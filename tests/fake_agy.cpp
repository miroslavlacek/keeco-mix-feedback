// Stand-in for the Antigravity CLI in tests: prints stream-json like `agy -p`.
// The prompt is echoed back so the test can check argument quoting (spaces, quotes, diacritics).
#include <juce_core/juce_core.h>
#include <iostream>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <shellapi.h>
 #pragma comment (lib, "shell32.lib")
#endif

static juce::StringArray getArgs (int argc, char** argv)
{
    juce::StringArray a;
   #if JUCE_WINDOWS
    juce::ignoreUnused (argc, argv);
    int n = 0;
    if (auto** w = CommandLineToArgvW (GetCommandLineW(), &n))
    {
        for (int i = 1; i < n; ++i) a.add (juce::String (w[i]));
        LocalFree (w);
    }
   #else
    for (int i = 1; i < argc; ++i) a.add (juce::String::fromUTF8 (argv[i]));
   #endif
    return a;
}

static void out (const juce::String& line) { std::cout << line.toStdString() << "\n" << std::flush; }

int main (int argc, char** argv)
{
    const auto args = getArgs (argc, argv);
    // `agy mcp add <name> <command>` / `agy mcp remove <name>` edit ~/.gemini/config/mcp_config.json
    if (args[0] == "mcp" && args.size() >= 3)
    {
        auto cfg = juce::File (juce::SystemStats::getEnvironmentVariable ("KMF_AGY_HOME", {})).getChildFile ("config").getChildFile ("mcp_config.json");
        auto root = juce::JSON::parse (cfg);
        if (root.getDynamicObject() == nullptr) root = juce::var (new juce::DynamicObject());
        auto servers = root.getProperty ("mcpServers", {});
        if (servers.getDynamicObject() == nullptr) { servers = juce::var (new juce::DynamicObject()); root.getDynamicObject()->setProperty ("mcpServers", servers); }
        if (args[1] == "remove")
        {
            if (! servers.getDynamicObject()->hasProperty (args[2])) { std::cerr << "server not found\n"; return 1; }
            servers.getDynamicObject()->removeProperty (args[2]);
        }
        else if (args[1] == "add" && args.size() >= 4)
        {
            auto* e = new juce::DynamicObject(); e->setProperty ("command", args[3]);
            servers.getDynamicObject()->setProperty (args[2], juce::var (e));
            std::cout << "Added MCP server \"" << args[2] << "\" (stdio)" << std::endl;
        }
        cfg.getParentDirectory().createDirectory();
        cfg.replaceWithText (juce::JSON::toString (root));
        return 0;
    }

    const int p = args.indexOf ("-p");
    const auto prompt = p >= 0 && p + 1 < args.size() ? args[p + 1] : juce::String();
    const int c = args.indexOf ("--conversation");
    const auto conv = c >= 0 && c + 1 < args.size() ? args[c + 1] + "-next" : juce::String ("conv-1");

    if (prompt.contains ("FAIL"))
    {
        std::cerr << "Error: authentication required. Run agy to sign in.\n";
        return 1;
    }
    auto j = [] (const juce::var& v) { return juce::JSON::toString (v, true); };
    auto o = [] (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto* d = new juce::DynamicObject();
        for (auto& p2 : kv) d->setProperty (p2.first, p2.second);
        return juce::var (d);
    };

    out (j (o ({ { "event", "init" }, { "cwd", juce::File::getCurrentWorkingDirectory().getFullPathName() } })));
    out (j (o ({ { "event", "step_update" }, { "conversation_id", conv }, { "step_index", 1 }, { "state", "ACTIVE" },
                 { "step_type", "tool" }, { "tool_name", "mixsight/get_mix_status" } })));
    out (j (o ({ { "event", "step_update" }, { "conversation_id", conv }, { "step_index", 1 }, { "state", "DONE" },
                 { "step_type", "tool" }, { "tool_name", "mixsight/get_mix_status" } })));
    out (j (o ({ { "event", "step_update" }, { "conversation_id", conv }, { "step_index", 2 }, { "state", "ACTIVE" },
                 { "step_type", "agent_response" }, { "text_delta", "**Drop** je " } })));
    out (j (o ({ { "event", "step_update" }, { "conversation_id", conv }, { "step_index", 2 }, { "state", "ACTIVE" },
                 { "step_type", "agent_response" }, { "text_delta", "mono.\n" } })));
    const auto last = prompt.fromLastOccurrenceOf ("\n", false, false)
                    + (prompt.contains ("STATUS {") ? juce::String::fromUTF8 (" [merania: áno]") : juce::String());
    out (j (o ({ { "event", "step_update" }, { "conversation_id", conv }, { "step_index", 2 }, { "state", "DONE" },
                 { "step_type", "agent_response" }, { "text_delta", "**Drop** je mono.\n* Dostal som: " + last } })));
    std::cerr << "info: 1 tool call\n";
    out (j (o ({ { "event", "result" }, { "conversation_id", conv }, { "status", "SUCCESS" },
                 { "response", "**Drop** je mono.\n* Dostal som: " + last } })));
    return 0;
}
