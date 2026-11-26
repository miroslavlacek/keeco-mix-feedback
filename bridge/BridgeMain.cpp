// KeecoMcpBridge – MCP server (stdio, JSON-RPC 2.0) for Antigravity CLI and the Claude desktop app.
// The AI client starts this program; it forwards tool calls to the Keeco Mix Feedback plugin
// running in the DAW over a local 127.0.0.1 connection. stdout carries only protocol
// messages; diagnostics go to stderr.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <string>
#include "../Source/McpLink.h"

namespace
{
constexpr const char* serverVersion = "0.4.0";
juce::String clientName;                 // from initialize; used to hide audio tools from clients that cannot listen

juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (const auto& p : props) o->setProperty (p.first, p.second);
    return juce::var (o);
}

// ---------------------------------------------------------------- plugin connection

class PluginClient : public juce::InterprocessConnection
{
public:
    PluginClient() : InterprocessConnection (false, kmflink::magic) {}
    ~PluginClient() override { disconnect(); }

    void connectionMade() override {}
    void connectionLost() override { lost = true; replied.signal(); }
    void messageReceived (const juce::MemoryBlock& m) override { reply = kmflink::fromBlock (m); replied.signal(); }

    juce::String reply;
    juce::WaitableEvent replied;
    std::atomic<bool> lost { false };
};

// Sends one request to the plugin. Returns the plugin's JSON reply, or an empty string.
juce::String askPlugin (const juce::String& cmd, const juce::var& args, juce::String& error)
{
    juce::Array<int> ports;
    const auto fromFile = kmflink::portFile().loadFileAsString().trim().getIntValue();
    if (fromFile > 0) ports.add (fromFile);
    for (int p = kmflink::basePort; p < kmflink::basePort + kmflink::portCount; ++p) ports.addIfNotAlreadyThere (p);

    const auto request = juce::JSON::toString (obj ({ { "cmd", cmd }, { "args", args } }), true);
    for (int port : ports)
    {
        PluginClient c;
        if (! c.connectToSocket ("127.0.0.1", port, 400))
            continue;
        if (! c.sendMessage (kmflink::toBlock (request)) || ! c.replied.wait (4000) || c.reply.isEmpty())
        {
            error = "The plugin did not answer in time.";
            return {};
        }
        return c.reply;
    }
    error = "Keeco Mix Feedback is not running. Open the DAW project with the plugin on the master channel "
            "(or the standalone app) and try again.";
    return {};
}

// ---------------------------------------------------------------- tools

struct Tool { const char* name; const char* cmd; const char* description; juce::var schema; bool needsListening = false; };

juce::var schema (std::initializer_list<std::pair<const char*, juce::var>> props, juce::StringArray required = {})
{
    auto s = obj ({ { "type", "object" }, { "properties", obj (props) } });
    if (! required.isEmpty())
    {
        juce::Array<juce::var> r; for (auto& x : required) r.add (x);
        s.getDynamicObject()->setProperty ("required", r);
    }
    return s;
}

std::vector<Tool> makeTools()
{
    auto intProp  = [] (const char* d) { return obj ({ { "type", "integer" }, { "description", d } }); };
    auto strProp  = [] (const char* d) { return obj ({ { "type", "string" },  { "description", d } }); };
    juce::Array<juce::var> modes { "premaster", "master" }, sev { "info", "warning", "problem" };

    return {
        { "get_mix_status", "status",
          "Live measurements from the Keeco Mix Feedback plugin on the DAW master: whether the DAW is playing, current bar, "
          "tempo, mode (premaster/master), loudness (LUFS momentary/short-term/integrated, LRA), true peak, PLR/PSR, "
          "stereo correlation and width, bass mono check below 120 Hz, and 5 band levels (with delta to the reference if one is active). "
          "Measurement restarts every time playback starts.",
          schema ({}) },
        { "get_sections", "sections",
          "Statistics for each 8-bar section since playback started (loudness, PSR, true peak, stereo, band balance, "
          "delta to reference). Best source for judging arrangement, drops vs breakdowns and how the mix develops.",
          schema ({ { "from_bar", intProp ("First bar to include (optional)") }, { "to_bar", intProp ("Last bar to include (optional)") } }) },
        { "get_alerts", "alerts",
          "Notes shown in the plugin's 'Postrehy' panel, newest first: automatic rule alerts (clipping, over-compression, "
          "stereo bass, phase, band vs reference, width contrast) and earlier insights posted by the AI.",
          schema ({ { "limit", intProp ("How many items, default 30") } }) },
        { "post_insight", "post_insight",
          "Show a short insight in the plugin window's 'Postrehy' panel, tagged with a bar number, so the producer sees it "
          "next to the meters while working in the DAW. Keep it to one or two sentences, in the user's language.",
          schema ({ { "text", strProp ("The insight, max ~300 characters") },
                    { "bar", intProp ("Bar it refers to (optional; defaults to the current bar)") },
                    { "severity", obj ({ { "type", "string" }, { "enum", sev }, { "description", "Default info" } }) } },
                  { "text" }) },
        { "set_mode", "set_mode",
          "Switch the plugin between 'premaster' (track goes to mastering: judge headroom and over-compression) and "
          "'master' (finished master).",
          schema ({ { "mode", obj ({ { "type", "string" }, { "enum", modes } }) } }, { "mode" }) },
        { "capture_audio", "capture_audio",
          "Save what the plugin heard on the master during the current playback as a WAV file (16 kHz mono), "
          "optionally only bars from_bar..to_bar, so you can LISTEN to it: after this call, read the returned file "
          "(read_many_files with the exact file path) and judge groove, arrangement, energy, sound design and vibe. "
          "Combine what you hear with get_sections numbers. Keep requests short (8-32 bars) unless asked otherwise.",
          schema ({ { "from_bar", intProp ("First bar (optional; default: start of playback)") },
                    { "to_bar", intProp ("Last bar (optional; default: end of what was played)") } }), true },
        { "list_references", "list_references",
          "List saved reference profiles (learned from reference tracks in the plugin) and which one is active.",
          schema ({}) },
        { "set_reference", "set_reference",
          "Activate a saved reference profile by name, or pass 'none' to switch the reference off.",
          schema ({ { "name", strProp ("Reference name from list_references, or 'none'") } }, { "name" }) },
    };
}

// ---------------------------------------------------------------- JSON-RPC

void send (const juce::var& message)
{
    const auto text = juce::JSON::toString (message, true);
    std::cout << text.toStdString() << "\n" << std::flush;
}

juce::var response (const juce::var& id, const juce::var& result)
{
    return obj ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } });
}

juce::var errorResponse (const juce::var& id, int code, const juce::String& msg)
{
    return obj ({ { "jsonrpc", "2.0" }, { "id", id }, { "error", obj ({ { "code", code }, { "message", msg } }) } });
}

juce::var toolResult (const juce::String& text, bool isError)
{
    juce::Array<juce::var> content { obj ({ { "type", "text" }, { "text", text } }) };
    return obj ({ { "content", content }, { "isError", isError } });
}

juce::var handle (const juce::var& msg, const std::vector<Tool>& tools)
{
    const auto method = msg["method"].toString();
    const auto id = msg["id"];
    const auto params = msg["params"];

    if (method == "initialize")
    {
        const auto requested = params["protocolVersion"].toString();
        clientName = params["clientInfo"]["name"].toString();
        std::cerr << "client: " << clientName << "\n";
        return response (id, obj ({
            { "protocolVersion", requested.isNotEmpty() ? requested : juce::String ("2025-06-18") },
            { "capabilities", obj ({ { "tools", obj ({ { "listChanged", false } }) } }) },
            { "serverInfo", obj ({ { "name", "keeco-mix-feedback" }, { "version", serverVersion } }) },
            { "instructions",
              "Tools for the Keeco Mix Feedback plugin on the DAW master. The user produces minimal deep drum & bass. "
              "Read get_mix_status / get_sections before giving mix advice; refer to bar numbers; when a finding is worth "
              "seeing in the DAW, also send a short version with post_insight. Numbers are measurements of what the "
              "plugin heard during the current playback only. If you can listen to audio, use capture_audio and then "
              "read the WAV file it returns to hear the music itself." } }));
    }

    if (method == "ping")
        return response (id, obj ({}));

    if (method == "tools/list")
    {
        juce::Array<juce::var> list;
        const bool canListen = ! clientName.containsIgnoreCase ("claude");   // Claude cannot hear audio files
        for (const auto& t : tools)
            if (canListen || ! t.needsListening)
                list.add (obj ({ { "name", t.name }, { "description", t.description }, { "inputSchema", t.schema } }));
        return response (id, obj ({ { "tools", list } }));
    }

    if (method == "tools/call")
    {
        const auto name = params["name"].toString();
        for (const auto& t : tools)
        {
            if (name != t.name) continue;
            juce::String error;
            auto args = params["arguments"].isObject() ? params["arguments"] : obj ({});
            if (juce::String (t.cmd) == "capture_audio")
            {
                // Captures go next to this program, so a CLI started in that folder can read them.
                const auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                     .getParentDirectory().getChildFile ("captures");
                args.getDynamicObject()->setProperty ("dir", dir.getFullPathName());
            }
            const auto raw = askPlugin (t.cmd, args, error);
            if (raw.isEmpty())
                return response (id, toolResult (error, true));
            const auto reply = juce::JSON::parse (raw);
            if (! (bool) reply["ok"])
                return response (id, toolResult (reply["error"].toString(), true));
            const auto result = reply["result"];
            const auto fmt = juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::singleLine).withMaxDecimalPlaces (2);
            auto text = result.isString() ? result.toString() : juce::JSON::toString (result, fmt);
            if (juce::String (t.cmd) == "capture_audio")
                text << "\nNext step: read this WAV file to listen to it, e.g. read_many_files(paths=[\""
                     << result["file"].toString().replace ("\\", "/") << "\"]).";
            return response (id, toolResult (text, false));
        }
        return errorResponse (id, -32602, "Unknown tool: " + name);
    }

    return errorResponse (id, -32601, "Method not found: " + method);
}
} // namespace

int main()
{
    std::ios::sync_with_stdio (false);
    const auto tools = makeTools();
    std::cerr << "keeco-mix-feedback MCP bridge " << serverVersion << " ready\n";

    std::string line;
    while (std::getline (std::cin, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const auto msg = juce::JSON::parse (juce::String::fromUTF8 (line.c_str()));
        if (! msg.isObject())
        {
            send (errorResponse (juce::var(), -32700, "Parse error"));
            continue;
        }
        if (! msg.hasProperty ("id"))       // notification (e.g. notifications/initialized): no reply
            continue;
        send (handle (msg, tools));
    }
    return 0;
}
