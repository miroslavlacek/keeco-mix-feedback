// Tests for the in-plugin AI chat: stream-json parsing, markdown cleanup,
// settings.json permission rule, and a full turn against a fake agy executable.
#include "AiChat.h"
#include <cstdlib>
#include <iostream>

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)

static void setEnv (const char* k, const juce::String& v)
{
   #if JUCE_WINDOWS
    _putenv_s (k, v.toRawUTF8());
   #else
    setenv (k, v.toRawUTF8(), 1);
   #endif
}

static juce::String allText (const AiChat& c, AiChat::Message::Role role)
{
    juce::String s;
    for (auto& m : c.getMessages()) if (m.role == role) s << m.text << "\n";
    return s;
}

static bool waitIdle (AiChat& c)
{
    for (int i = 0; i < 300; ++i) { if (! c.isBusy()) return true; juce::Thread::sleep (50); }
    return false;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: kmf_chat_tests <path-to-kmf_fake_agy>\n"; return 2; }
    const juce::File fake (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])));
    CHECK (fake.existsAsFile());

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kmf_chat_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
    auto home = tmp.getChildFile (".gemini"), ws = tmp.getChildFile ("Keeco Ws");
    home.getChildFile ("config").createDirectory(); ws.createDirectory();
    const auto bridge = ws.getChildFile ("KeecoMcpBridge.exe");
    home.getChildFile ("config").getChildFile ("mcp_config.json")
        .replaceWithText ("{ \"mcpServers\": { \"keeco-mix-feedback\": { \"command\": " + juce::JSON::toString (bridge.getFullPathName()) + " } } }");
    setEnv ("KMF_AGY_HOME", home.getFullPathName());
    setEnv ("KMF_AGY_PATH", fake.getFullPathName());

    // --- markdown cleanup
    CHECK (AiChat::cleanMarkdown (juce::String::fromUTF8 ("## Nadpis\n**tučné** a `kód`\n* bod\n  - pod")) == juce::String::fromUTF8 ("Nadpis\ntučné a kód\n• bod\n  • pod"));

    // --- parsing: deltas, DONE with full text, tool bubble split, result
    {
        AiChat c;
        AiChat::TurnState st;
        c.handleLine (st, R"({"event":"step_update","conversation_id":"x1","step_index":0,"state":"ACTIVE","step_type":"agent_response","text_delta":"Pozriem "})");
        c.handleLine (st, R"({"event":"step_update","conversation_id":"x1","step_index":0,"state":"DONE","step_type":"agent_response","text_delta":"Pozriem sa."})");
        c.handleLine (st, R"({"event":"step_update","step_index":1,"state":"ACTIVE","step_type":"tool","tool_name":"keeco-mix-feedback/capture_audio"})");
        c.handleLine (st, R"({"event":"step_update","step_index":1,"state":"DONE","step_type":"tool","tool_name":"keeco-mix-feedback/capture_audio"})");
        c.handleLine (st, R"({"event":"step_update","step_index":2,"state":"ACTIVE","step_type":"agent_response","text_delta":"Drop "})");
        c.handleLine (st, R"({"event":"step_update","step_index":2,"state":"ACTIVE","step_type":"agent_response","text_delta":"udrie."})");
        c.handleLine (st, R"({"event":"result","conversation_id":"x1","status":"SUCCESS","response":"Pozriem sa. Drop udrie."})");
        const auto msgs = c.getMessages();
        CHECK (msgs.size() == 1);                      // tool calls are not listed in the chat
        if (msgs.size() == 1)
            CHECK (msgs[0].role == AiChat::Message::Role::ai && msgs[0].text == "Pozriem sa.\n\nDrop udrie.");
        CHECK (st.conversationId == "x1" && st.status == "SUCCESS");

        AiChat::TurnState plain;                       // non-JSON output is shown as text
        AiChat c2;
        c2.handleLine (plain, "Ahoj, toto je text.");
        CHECK (allText (c2, AiChat::Message::Role::ai).contains ("Ahoj, toto je text."));
    }

    // --- tool descriptions for the status line
    {
        juce::var params (new juce::DynamicObject());
        params.getDynamicObject()->setProperty ("server", "keeco-mix-feedback");
        params.getDynamicObject()->setProperty ("tool_name", "capture_audio");
        CHECK (AiChat::describeTool ("call_mcp_tool", params).startsWith ("Nahr"));
        juce::var rp (new juce::DynamicObject());
        rp.getDynamicObject()->setProperty ("AbsolutePath", "C:/KeecoMixFeedback/captures/keeco_bars_33-48.wav");
        CHECK (AiChat::describeTool ("read_file", rp).contains ("nahr"));
        CHECK (AiChat::describeTool ("read_file", juce::var()).contains ("podklady"));
        CHECK (AiChat::describeTool ("something", juce::var()).isNotEmpty());
    }

    // --- full turn against the fake agy (process runner, quoting, conversation id)
    {
        AiChat c;
        CHECK (c.getSetupProblem().isEmpty());
        CHECK (c.getWorkspace() == ws);
        const auto prompt = juce::String::fromUTF8 ("Vypočuj si takty 33–48, \"drop\" C:\\cesta\\ ťažký bas");
        CHECK (c.send (prompt, "STATUS {\"bar\": 40}"));
        CHECK (waitIdle (c));
        const auto ai = allText (c, AiChat::Message::Role::ai);
        std::cout << "AI reply: " << ai.toStdString();
        CHECK (ai.contains ("Drop je mono."));
        CHECK (ai.contains (prompt));
        CHECK (ai.contains (juce::String::fromUTF8 ("[merania: áno]")));
        CHECK (allText (c, AiChat::Message::Role::tool).isEmpty());
        CHECK (allText (c, AiChat::Message::Role::error).isEmpty());
        CHECK (c.getConversationId() == "conv-1");

        CHECK (c.send ("Druha otazka"));
        CHECK (waitIdle (c));
        CHECK (c.getConversationId() == "conv-1-next");     // second turn passed --conversation conv-1

        CHECK (c.send ("FAIL please"));
        CHECK (waitIdle (c));
        const auto err = allText (c, AiChat::Message::Role::error);
        std::cout << "Error shown: " << err.toStdString();
        CHECK (err.contains ("authentication required"));
        CHECK (err.contains (juce::String::fromUTF8 ("prihlás")));

        c.newConversation();
        CHECK (c.getMessages().empty() && c.getConversationId().isEmpty());
    }

    // --- permission rule in settings.json (keeps other keys, makes a backup)
    {
        AiChat c;
        auto settings = home.getChildFile ("antigravity-cli").getChildFile ("settings.json");
        settings.getParentDirectory().createDirectory();
        settings.replaceWithText ("{ \"colorScheme\": \"tokyo night\", \"permissions\": { \"allow\": [\"command(git)\"] } }");
        CHECK (! c.toolsAllowed());
        juce::String e;
        CHECK (c.allowTools (e));
        CHECK (c.toolsAllowed());
        const auto v = juce::JSON::parse (settings);
        CHECK (v["colorScheme"].toString() == "tokyo night");
        CHECK (v["permissions"]["allow"].size() == 2);
        CHECK (settings.getSiblingFile ("settings.json.bak").existsAsFile());
        CHECK (c.allowTools (e) && juce::JSON::parse (settings)["permissions"]["allow"].size() == 2);   // idempotent

        settings.replaceWithText ("{ broken json");
        CHECK (! c.allowTools (e) && e.isNotEmpty());
        CHECK (settings.loadFileAsString() == "{ broken json");                    // untouched
    }

    // --- missing agy
    {
        setEnv ("KMF_AGY_PATH", tmp.getChildFile ("nope.exe").getFullPathName());
        setEnv ("PATH", tmp.getFullPathName());
        setEnv ("LOCALAPPDATA", tmp.getFullPathName());
        AiChat c;
        CHECK (c.getSetupProblem().contains ("agy"));
        CHECK (! c.send ("ahoj"));
        CHECK (allText (c, AiChat::Message::Role::error).contains ("Antigravity"));
    }

    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "chat tests: all passed\n" : "chat tests: FAILED\n");
    return failures == 0 ? 0 : 1;
}
