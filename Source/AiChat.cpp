#include "AiChat.h"
#include "ProcessRunner.h"

namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

// Sent once at the start of every new conversation from the plugin window.
const char* firstTurnPreamble =
    "[Správa z chatu v okne pluginu Keeco Mix Feedback na mastri vo FL Studiu. "
    "Používateľ robí minimal deep drum & bass. Na čísla používaj nástroje servera keeco-mix-feedback "
    "(get_mix_status, get_sections, get_alerts); keď sa pýta na zvuk, vypočuj si takty cez capture_audio "
    "a prečítaj vrátený WAV. Odpovedaj po slovensky, stručne, s číslami taktov. Nepíš tabuľky. "
    "Najdôležitejší postreh môžeš zapísať do pluginu cez post_insight.]\n\n";
}

// ---------------------------------------------------------------- worker

class AiChat::Worker : public juce::Thread
{
public:
    Worker (AiChat& o, juce::String p) : juce::Thread ("Keeco AI chat"), owner (o), prompt (std::move (p)) {}
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
    if (worker != nullptr) worker->cancel();
    worker.reset();
}

// ---------------------------------------------------------------- setup

juce::File AiChat::agyHome()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("KMF_AGY_HOME", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".gemini");
}

void AiChat::refreshSetup()
{
    // 1) agy executable
    agyExe = juce::File();
    const auto envExe = juce::SystemStats::getEnvironmentVariable ("KMF_AGY_PATH", {});
    if (envExe.isNotEmpty()) agyExe = juce::File (envExe);
   #if JUCE_WINDOWS
    if (! agyExe.existsAsFile())
    {
        const auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
        if (local.isNotEmpty()) agyExe = juce::File (local).getChildFile ("agy\\bin\\agy.exe");
    }
    const juce::String exeName = "agy.exe"; const char sep = ';';
   #else
    const juce::String exeName = "agy"; const char sep = ':';
   #endif
    if (! agyExe.existsAsFile())
    {
        juce::StringArray dirs;
        dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), juce::String::charToString (sep), {});
        for (auto& d : dirs)
            if (d.isNotEmpty() && juce::File::isAbsolutePath (d) && juce::File (d).getChildFile (exeName).existsAsFile())
                { agyExe = juce::File (d).getChildFile (exeName); break; }
    }

    // 2) workspace = folder of KeecoMcpBridge from agy's mcp_config.json
    workspace = juce::File();
    const auto envWs = juce::SystemStats::getEnvironmentVariable ("KMF_AI_WORKSPACE", {});
    if (envWs.isNotEmpty()) workspace = juce::File (envWs);

    const auto cfg = juce::JSON::parse (agyHome().getChildFile ("config").getChildFile ("mcp_config.json"));
    if (auto* servers = cfg.getProperty ("mcpServers", {}).getDynamicObject())
    {
        for (auto& p : servers->getProperties())
        {
            const auto cmd = p.value.getProperty ("command", {}).toString();
            if (cmd.containsIgnoreCase ("KeecoMcpBridge"))
            {
                serverName = p.name.toString();
                if (! workspace.isDirectory() && juce::File::isAbsolutePath (cmd))
                    workspace = juce::File (cmd).getParentDirectory();
                break;
            }
        }
    }
   #if JUCE_WINDOWS
    if (! workspace.isDirectory() && juce::File ("C:\\KeecoMixFeedback").isDirectory())
        workspace = juce::File ("C:\\KeecoMixFeedback");
   #endif
}

juce::String AiChat::getSetupProblem() const
{
    if (! agyExe.existsAsFile())
        return u8 ("Nenašiel som Antigravity CLI (agy.exe). Nainštaluj ho: irm https://antigravity.google/cli/install.ps1 | iex");
    if (! workspace.isDirectory())
        return u8 ("Most KeecoMcpBridge nie je v Antigravity pridaný. V PowerShelli spusti: "
                   "agy mcp add keeco-mix-feedback C:\\KeecoMixFeedback\\KeecoMcpBridge.exe");
    return {};
}

static juce::File agySettingsFile() { return AiChat::agyHome().getChildFile ("antigravity-cli").getChildFile ("settings.json"); }

bool AiChat::toolsAllowed() const
{
    const auto rule = "mcp(" + serverName + "/*)";
    const auto settings = juce::JSON::parse (agySettingsFile());
    if (settings.getProperty ("toolPermission", {}).toString() == "always-proceed") return true;
    if (auto* allow = settings.getProperty ("permissions", {}).getProperty ("allow", {}).getArray())
        for (auto& v : *allow)
            if (v.toString() == rule) return true;
    return false;
}

bool AiChat::allowTools (juce::String& error)
{
    const auto file = agySettingsFile();
    juce::var root;
    if (file.existsAsFile() && file.loadFileAsString().trim().isNotEmpty())
    {
        const auto res = juce::JSON::parse (file.loadFileAsString(), root);
        if (res.failed() || root.getDynamicObject() == nullptr)
            { error = u8 ("settings.json sa nedá prečítať (") + res.getErrorMessage() + u8 ("), nemením ho."); return false; }
    }
    else
        root = juce::var (new juce::DynamicObject());

    auto* obj = root.getDynamicObject();
    juce::var perms = obj->getProperty ("permissions");
    if (perms.getDynamicObject() == nullptr) { perms = juce::var (new juce::DynamicObject()); obj->setProperty ("permissions", perms); }
    juce::var allow = perms.getProperty ("allow", {});
    if (! allow.isArray()) { allow = juce::var (juce::Array<juce::var>()); }
    const auto rule = "mcp(" + serverName + "/*)";
    bool present = false;
    for (auto& v : *allow.getArray()) present = present || v.toString() == rule;
    if (! present) allow.getArray()->add (rule);
    perms.getDynamicObject()->setProperty ("allow", allow);

    file.getParentDirectory().createDirectory();
    if (file.existsAsFile()) file.copyFileTo (file.getSiblingFile ("settings.json.bak"));
    if (! file.replaceWithText (juce::JSON::toString (root)))
        { error = u8 ("Nepodarilo sa zapísať ") + file.getFullPathName(); return false; }
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

bool AiChat::send (const juce::String& text)
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
    worker = std::make_unique<Worker> (*this, (first ? u8 (firstTurnPreamble) : juce::String()) + text.trim());
    aiMessageIndex = -1;
    status = u8 ("Premýšľam…");
    ++version;
    worker->startThread();
    return true;
}

// ---------------------------------------------------------------- one turn (worker thread)

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
            if (name.contains ("/")) name = name.fromLastOccurrenceOf ("/", false, false);
            if (name.isEmpty()) name = "tool";
            if (st.toolStepsSeen.insert (idx).second)
            {
                st.lastToolStep = juce::jmax (st.lastToolStep, idx);
                addMessage (Message::Role::tool, name);
                { const juce::ScopedLock sl (lock); aiMessageIndex = -1; }   // text after a tool call starts a new bubble
                setStatus (name == "capture_audio" ? u8 ("Nahrávam a počúvam takty…")
                                                   : u8 ("Volám ") + name + u8 ("…"));
            }
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
            for (auto& kv : st.stepText) if (kv.first > st.lastToolStep && kv.second.isNotEmpty()) all += (all.isEmpty() ? "" : "\n\n") + kv.second;
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
        if (msg.containsIgnoreCase ("auth") || msg.containsIgnoreCase ("login") || msg.containsIgnoreCase ("sign in"))
            msg += u8 ("\nSpusti raz agy v PowerShelli a prihlás sa cez Google, potom skús znova.");
        addMessage (Message::Role::error, msg);
    }

    if (stderrText.containsIgnoreCase ("permission") || stderrText.containsIgnoreCase ("denied") || stderrText.containsIgnoreCase ("not allowed"))
        if (! toolsAllowed())
            addMessage (Message::Role::info, u8 ("Antigravity odmietol niektorý nástroj bez povolenia. Klikni na „Povoliť nástroje pluginu“."));

    { const juce::ScopedLock sl (lock); aiMessageIndex = -1; }
    setStatus ({});
}
