#pragma once
#include <juce_core/juce_core.h>

// Everything needed to get Antigravity (agy) ready for MixSight:
// find agy, register the bridge as MCP server "mixsight", allow its tools,
// open the Google sign-in window. Shared by the plugin and MixSightHelper.exe.
namespace aisetup
{
juce::File agyHome();                                 // ~/.gemini (KMF_AGY_HOME overrides)
juce::File findAgy();                                 // agy.exe or empty
juce::File mcpConfigFile();                           // ~/.gemini/config/mcp_config.json
juce::File settingsFile();                            // ~/.gemini/antigravity-cli/settings.json

// Name and bridge path of the registered MixSight server (also finds the old "keeco-mix-feedback" entry).
struct Registration { juce::String name; juce::File bridge; };
Registration findRegistration();

bool registerBridge (const juce::File& agy, const juce::File& bridge, juce::String& log);
bool toolsAllowed (const juce::String& serverName);
bool allowTools (const juce::String& serverName, juce::String& error);

bool openSignInWindow (const juce::File& agy, const juce::File& workspace, juce::String& error);
bool openAgyInstaller (juce::String& error);

int runAndCapture (const juce::StringArray& args, const juce::File& cwd, juce::String& output);   // exit code
}
