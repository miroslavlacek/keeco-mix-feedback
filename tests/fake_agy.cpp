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
                 { "step_type", "tool" }, { "tool_name", "keeco-mix-feedback/get_mix_status" } })));
    out (j (o ({ { "event", "step_update" }, { "conversation_id", conv }, { "step_index", 1 }, { "state", "DONE" },
                 { "step_type", "tool" }, { "tool_name", "keeco-mix-feedback/get_mix_status" } })));
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
