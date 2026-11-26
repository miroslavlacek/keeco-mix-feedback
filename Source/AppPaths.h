#pragma once
#include <juce_core/juce_core.h>

// Where MixSight keeps its files.
//   appData()    %APPDATA%\MixSight            references, link port, settings
//   installDir() folder that contains MixSight.vst3, MixSightBridge.exe, GEMINI.md
//                (C:\Program Files\Common Files\VST3\MixSight with the installer)
namespace mixsight
{
constexpr const char* productName = "MixSight";
constexpr const char* serverName  = "mixsight";            // MCP server name in Antigravity
constexpr const char* bridgeExe   = "MixSightBridge.exe";
constexpr const char* helperExe   = "MixSightHelper.exe";
constexpr const char* githubRepo  = "miroslavlacek/keeco-mix-feedback";

inline juce::File appData()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (productName);
    if (! dir.isDirectory())
    {
        dir.createDirectory();
        // one-time migration from the old name
        auto old = dir.getSiblingFile ("Keeco Mix Feedback").getChildFile ("References");
        if (old.isDirectory()) old.copyDirectoryTo (dir.getChildFile ("References"));
    }
    return dir;
}

// Folder of the plugin bundle (…\MixSight\MixSight.vst3\Contents\x86_64-win\MixSight.vst3 → …\MixSight).
inline juce::File installDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("MIXSIGHT_INSTALL_DIR", {});
    if (env.isNotEmpty()) return juce::File (env);
    auto f = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto p = f; p.getParentDirectory() != p; p = p.getParentDirectory())
        if (p.getFileExtension().equalsIgnoreCase (".vst3") && p.isDirectory())
            return p.getParentDirectory();
    return f.getParentDirectory();          // standalone app or bridge: folder of the exe
}

inline juce::String versionString()
{
   #ifdef MIXSIGHT_VERSION
    return MIXSIGHT_VERSION;
   #else
    return "0.0.0";
   #endif
}
} // namespace mixsight
