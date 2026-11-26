#include "AiChat.h"
#include "ProcessRunner.h"
#include "AiSetup.h"
#include "AppPaths.h"

namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

// Sent once at the start of every new conversation from the plugin window.
const char* firstTurnPreamble =
    "[Správa z chatu v okne pluginu MixSight na mastri v DAW. Nástroje sú na serveri mixsight. "
    "Aktuálne merania (STATUS, SECTIONS, ALERTS) ti plugin posiela priamo v každej správe, takže "
    "get_mix_status, get_sections ani get_alerts kvôli nim nevolaj a nečítaj schémy nástrojov. "
    "Keď sa pýta na zvuk, groove alebo energiu, zavolaj capture_audio {from_bar, to_bar} a prečítaj vrátený WAV. "
    "Odpovedaj po slovensky, stručne (do ~10 riadkov), s číslami taktov, bez tabuliek a nadpisov. "
    "post_insight použi len pri vážnom probléme alebo keď o to používateľ požiada.]\n\n";
}

// ---------------------------------------------------------------- worker

class AiChat::Worker : public juce::Thread
{
public:
    Worker (AiChat& o, juce::String p) : juce::Thread ("MixSight AI chat"), owner (o), prompt (std::move (p)) {}
    ~Worker() override { runner.kill(); stopThread (4000); }

    void run() override { owner.runTurn (prompt); }
    void cancel() { signalThreadShouldExit(); runner.kill(); }

    kmf::ProcessRunner runner;

private:
    AiChat& owner;
    juce::String prompt;
};

AiChat::AiChat() { refreshSetup(); }

AiChat::~AiChat()
{
    if (signInThread != nullptr) signInThread->stopThread (3000);
    if (worker != nullptr) worker->cancel();
    worker.reset();
}

// ---------------------------------------------------------------- setup

juce::File AiChat::agyHome() { return aisetup::agyHome(); }

void AiChat::refreshSetup()
{
    agyExe = aisetup::findAgy();
    const auto reg = aisetup::findRegistration();
    registered = reg.name.isNotEmpty() && reg.bridge.existsAsFile();
    if (reg.name.isNotEmpty()) serverName = reg.name;

    // Workspace = MixSight install folder (bridge + GEMINI.md); the registered bridge's folder otherwise.
    bridgeExe = mixsight::installDir().getChildFile (mixsight::bridgeExe);
    const auto envWs = juce::SystemStats::getEnvironmentVariable ("KMF_AI_WORKSPACE", {});
    if (envWs.isNotEmpty())                 workspace = juce::File (envWs);
    else if (bridgeExe.existsAsFile())      workspace = bridgeExe.getParentDirectory();
    else if (reg.bridge.existsAsFile())     workspace = reg.bridge.getParentDirectory();
    else                                    workspace = juce::File();
    if (! bridgeExe.existsAsFile() && reg.bridge.existsAsFile()) bridgeExe = reg.bridge;
    signedInCached = aisetup::isSignedIn();
}

bool AiChat::needsSignIn() const { return signInNeeded.load() || ! signedInCached; }

juce::String AiChat::getSetupProblem() const
{
    if (! agyExe.existsAsFile())
        return u8 ("Na tomto počítači chýba Antigravity CLI (Gemini). Klikni na „Nainštalovať Antigravity“.");
    if (! registered || ! workspace.isDirectory())
        return u8 ("Plugin ešte nie je pripojený k Antigravity. Klikni na „Pripojiť AI k pluginu“.");
    return {};
}

AiChat::SetupStep AiChat::getSetupStep() const
{
    if (! agyExe.existsAsFile()) return SetupStep::installAgy;
    if (! registered || ! workspace.isDirectory()) return SetupStep::connect;
    if (! toolsAllowed()) return SetupStep::allowTools;
    if (needsSignIn()) return SetupStep::signIn;
    return SetupStep::ready;
}

bool AiChat::toolsAllowed() const { return aisetup::toolsAllowed (serverName); }
bool AiChat::allowTools (juce::String& error) { return aisetup::allowTools (serverName, error); }

bool AiChat::connectBridge (juce::String& log)
{
    refreshSetup();
    if (! bridgeExe.existsAsFile()) { log = u8 ("Nenašiel som ") + mixsight::bridgeExe + u8 (" vedľa pluginu – preinštaluj MixSight."); return false; }
    const bool ok = aisetup::registerBridge (agyExe, bridgeExe, log);
    refreshSetup();
    if (ok) { juce::String e; aisetup::allowTools (serverName, e); }
    return ok;
}

bool AiChat::openSignIn (juce::String& error)
{
    refreshSetup();
    const auto ws = workspace.isDirectory() ? workspace : mixsight::appData();

    if (! needsSignIn())
    {
        // Already signed in: plain window for switching the account.
        const bool ok = aisetup::openSignInWindow (agyExe, ws, error);
        if (ok)
            addMessage (Message::Role::info,
                        u8 ("Si prihlásený. Na zmenu účtu v otvorenom okne napíš /logout, prihlás sa iným účtom a okno zavri príkazom /quit."));
        return ok;
    }
    if (isSigningIn()) return true;

    struct SignIn : juce::Thread
    {
        SignIn (AiChat& o, juce::File a, juce::File w) : juce::Thread ("MixSight sign-in"), owner (o), agy (a), ws (w) {}
        void run() override
        {
            juce::String err;
            const bool ok = aisetup::signInAndWait (agy, ws, 600, err, [this] { return threadShouldExit(); });
            if (threadShouldExit()) return;
            owner.signInNeeded = ! ok;
            owner.signedInCached = ok;
            owner.addMessage (ok ? Message::Role::info : Message::Role::error,
                              ok ? u8 ("✓ Prihlásený do Gemini. Môžeš sa pýtať.")
                                 : u8 ("Prihlásenie sa nedokončilo (") + err + u8 ("). Skús znova tlačidlom „Prihlásiť sa cez Google“."));
            owner.setStatus ({});
        }
        AiChat& owner; juce::File agy, ws;
    };

    if (signInThread != nullptr) signInThread->stopThread (3000);
    signInThread = std::make_unique<SignIn> (*this, agyExe, ws);
    addMessage (Message::Role::info,
                u8 ("Otvára sa okno Antigravity a prehliadač. V okne vyber „Login with Google“ a prihlás sa v prehliadači – "
                    "okno sa po prihlásení zavrie samo."));
    setStatus (u8 ("Čakám na prihlásenie…"));
    signInThread->startThread();
    return true;
}

// ---------------------------------------------------------------- state

bool AiChat::isBusy() const
{
    const juce::ScopedLock sl (lock);
    return worker != nullptr && worker->isThreadRunning();
}

juce::String AiChat::getStatus() const { const juce::ScopedLock sl (lock); return status; }
juce::String AiChat::getConversationId() const { const juce::ScopedLock sl (lock); return conversationId; }

std::vector<AiChat::Message> AiChat::getMessages() const
{
    const juce::ScopedLock sl (lock);
    return messages;
}

void AiChat::addMessage (Message::Role role, const juce::String& text)
{
    const juce::ScopedLock sl (lock);
    messages.push_back ({ role, text });
    if (messages.size() > 400) { messages.erase (messages.begin(), messages.begin() + 100); aiMessageIndex = -1; }
    ++version;
}

void AiChat::setLastAiText (const juce::String& text)
{
    const juce::ScopedLock sl (lock);
    if (aiMessageIndex < 0 || aiMessageIndex >= (int) messages.size())
    {
        messages.push_back ({ Message::Role::ai, text });
        aiMessageIndex = (int) messages.size() - 1;
    }
    else
        messages[(size_t) aiMessageIndex].text = text;
    ++version;
}

void AiChat::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (lock);
    status = s;
    ++version;
}

void AiChat::newConversation()
{
    cancel();
    const juce::ScopedLock sl (lock);
    messages.clear(); conversationId.clear(); aiMessageIndex = -1; status.clear();
    ++version;
}

void AiChat::cancel()
{
    std::unique_ptr<Worker> w;
    {
        const juce::ScopedLock sl (lock);
        w = std::move (worker);
    }
    if (w != nullptr) { w->cancel(); w.reset(); }
}

bool AiChat::send (const juce::String& text, const juce::String& measurements, const juce::String& genre)
{
    if (text.trim().isEmpty() || isBusy()) return false;
    refreshSetup();

    std::unique_ptr<Worker> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (worker);
    }
    old.reset();

    addMessage (Message::Role::user, text.trim());
    const auto problem = getSetupProblem();
    if (problem.isNotEmpty()) { addMessage (Message::Role::error, problem); return false; }

    const juce::ScopedLock sl (lock);
    const bool first = conversationId.isEmpty();
    juce::String prompt = first ? u8 (firstTurnPreamble) : juce::String();
    if (genre.trim().isNotEmpty())
        prompt << u8 ("[Žáner, na ktorom používateľ pracuje: ") << genre.trim() << "]\n";
    if (measurements.isNotEmpty())
        prompt << u8 ("[Merania z pluginu práve teraz:]\n") << measurements << u8 ("\n[Koniec meraní]\n\n");
    prompt << u8 ("Otázka používateľa: ") << text.trim();
    worker = std::make_unique<Worker> (*this, prompt);
    aiMessageIndex = -1;
    status = u8 ("Premýšľam…");
    ++version;
    worker->startThread();
    return true;
}

// ---------------------------------------------------------------- one turn (worker thread)

juce::String AiChat::describeTool (const juce::String& rawName, const juce::var& params)
{
    auto name = rawName.fromLastOccurrenceOf ("/", false, false);
    // call_mcp_tool hides the real tool in its parameters; read_file has a path.
    juce::String inner, path;
    if (auto* o = params.getDynamicObject())
        for (auto& kv : o->getProperties())
        {
            const auto key = kv.name.toString().toLowerCase();
            const auto val = kv.value.toString();
            if (key.contains ("path") || key.contains ("file")) path = val;
            else if (key == "tool" || key == "tool_name" || key == "toolname" || key == "name") inner = val;
        }
    if (params.isString()) path = params.toString();
    const auto all = (name + " " + inner + " " + path + " " + juce::JSON::toString (params, true)).toLowerCase();

    if (all.contains ("capture_audio"))  return u8 ("Nahrávam takty…");
    if (all.contains (".wav"))           return u8 ("Počúvam nahrávku…");
    if (all.contains ("post_insight"))   return u8 ("Zapisujem postreh do pluginu…");
    if (all.contains ("get_mix_status") || all.contains ("get_sections") || all.contains ("get_alerts"))
        return u8 ("Čítam merania z pluginu…");
    if (name.containsIgnoreCase ("read") || name.containsIgnoreCase ("view") || name.containsIgnoreCase ("list"))
        return u8 ("Čítam podklady…");
    return u8 ("Pracujem…");
}

juce::String AiChat::cleanMarkdown (const juce::String& s)
{
    juce::StringArray lines;
    lines.addLines (s.replace ("**", "").replace ("__", "").replace ("`", ""));
    for (auto& l : lines)
    {
        auto t = l.trimStart();
        const int indent = l.length() - t.length();
        if (t.startsWith ("#")) { t = t.trimCharactersAtStart ("#").trimStart(); l = t; continue; }
        if (t.startsWith ("* ") || t.startsWith ("- ")) l = juce::String::repeatedString (" ", indent) + u8 ("• ") + t.substring (2);
    }
    return lines.joinIntoString ("\n").trim();
}

void AiChat::handleLine (TurnState& st, const juce::String& rawLine)
{
    const auto line = rawLine.trim();
    if (line.isEmpty()) return;

    juce::var ev;
    if (! line.startsWith ("{") || juce::JSON::parse (line, ev).failed() || ! ev.isObject())
    {
        // Not stream-json (older agy or plain text): treat as reply text.
        if (! st.sawJson)
        {
            st.responseText += (st.responseText.isEmpty() ? "" : "\n") + rawLine;
            setLastAiText (cleanMarkdown (st.responseText));
        }
        return;
    }
    st.sawJson = true;
    const auto event = ev.getProperty ("event", ev.getProperty ("type", {})).toString();

    // Some builds wrap the payload, some don't.
    juce::var p = ev;
    for (auto* key : { "payload", "data", "step_update", "result", "init" })
        if (ev.getProperty (key, {}).isObject()) { p = ev.getProperty (key, {}); break; }

    const auto cid = p.getProperty ("conversation_id", ev.getProperty ("conversation_id", {})).toString();
    if (cid.isNotEmpty()) st.conversationId = cid;

    if (event == "step_update")
    {
        const auto type = p.getProperty ("step_type", {}).toString();
        const int idx = (int) p.getProperty ("step_index", -1);
        const auto state = p.getProperty ("state", {}).toString();

        if (type == "tool")
        {
            auto name = p.getProperty ("tool_name", p.getProperty ("tool_info", {}).getProperty ("name", {})).toString();
            const auto params = p.getProperty ("tool_info", {}).getProperty ("parameters", p.getProperty ("parameters", {}));
            st.toolStepsSeen.insert (idx);
            setStatus (describeTool (name, params));
            const auto err = p.getProperty ("tool_info", {}).getProperty ("error", {});
            if (state == "DONE" && err.isObject())
                addMessage (Message::Role::error, name + ": " + err.getProperty ("message", {}).toString());
            return;
        }

        if (type == "agent_response")
        {
            const auto delta = p.getProperty ("text_delta", {}).toString();
            auto& acc = st.stepText[idx];
            if (state == "DONE" && acc.isNotEmpty() && delta.startsWith (acc)) acc = delta;   // DONE carries full text
            else acc += delta;

            juce::String all;
            for (auto& kv : st.stepText) if (kv.second.isNotEmpty()) all += (all.isEmpty() ? "" : "\n\n") + kv.second;
            if (all.isNotEmpty())
            {
                setLastAiText (cleanMarkdown (all));
                setStatus (u8 ("Píšem odpoveď…"));
            }
        }
        return;
    }

    if (event == "result")
    {
        st.gotResult = true;
        st.status = p.getProperty ("status", {}).toString();
        const auto resp = p.getProperty ("response", {}).toString();
        if (resp.isNotEmpty()) st.responseText = resp;
        const auto err = p.getProperty ("error", {});
        st.error = err.isObject() ? err.getProperty ("message", juce::JSON::toString (err, true)).toString() : err.toString();
    }
}

void AiChat::runTurn (const juce::String& prompt)
{
    juce::String convId;
    { const juce::ScopedLock sl (lock); convId = conversationId; }

    std::vector<std::string> args { agyExe.getFullPathName().toStdString(),
                                    "--output-format", "stream-json", "--print-timeout", "10m" };
    if (convId.isNotEmpty()) { args.push_back ("--conversation"); args.push_back (convId.toStdString()); }
    args.push_back ("-p");
    args.push_back (prompt.toStdString());

    TurnState st;
    auto* self = dynamic_cast<Worker*> (juce::Thread::getCurrentThread());
    if (self == nullptr) return;

    const int code = self->runner.run (args, workspace.getFullPathName().toStdString(),
                                       [this, &st] (const std::string& l) { handleLine (st, juce::String::fromUTF8 (l.c_str())); });
    if (self->threadShouldExit() || self->runner.wasKilled())
    {
        setStatus ({});
        addMessage (Message::Role::info, u8 ("Zastavené."));
        return;
    }

    if (st.conversationId.isNotEmpty()) { const juce::ScopedLock sl (lock); conversationId = st.conversationId; }
    if (st.responseText.isNotEmpty() && st.stepText.empty()) setLastAiText (cleanMarkdown (st.responseText));

    const auto stderrText = juce::String::fromUTF8 (self->runner.getStderr().c_str()).trim();
    if (code < 0)
        addMessage (Message::Role::error, u8 ("Antigravity sa nepodarilo spustiť: ") + juce::String (self->runner.getError()));
    else if (st.error.isNotEmpty() || (st.status.isNotEmpty() && st.status != "SUCCESS")
             || (code != 0 && st.responseText.isEmpty()) || (st.responseText.isEmpty() && st.stepText.empty()))
    {
        juce::String msg = st.error.isNotEmpty() ? st.error : stderrText.getLastCharacters (600);
        if (msg.isEmpty()) msg = u8 ("Antigravity skončil bez odpovede (kód ") + juce::String (code) + ").";
        if (msg.containsIgnoreCase ("auth") || msg.containsIgnoreCase ("login") || msg.containsIgnoreCase ("sign in")
            || stderrText.containsIgnoreCase ("authentication"))
        {
            signInNeeded = true;
            msg += u8 ("\nNie si prihlásený do Gemini – klikni na „Prihlásiť sa cez Google“.");
        }
        addMessage (Message::Role::error, msg);
    }

    if (stderrText.containsIgnoreCase ("permission") || stderrText.containsIgnoreCase ("denied") || stderrText.containsIgnoreCase ("not allowed"))
        if (! toolsAllowed())
            addMessage (Message::Role::info, u8 ("Antigravity odmietol niektorý nástroj bez povolenia. Klikni na „Povoliť nástroje pluginu“."));
    if (code == 0 && (st.responseText.isNotEmpty() || ! st.stepText.empty())) signInNeeded = false;

    { const juce::ScopedLock sl (lock); aiMessageIndex = -1; }
    setStatus ({});
}
