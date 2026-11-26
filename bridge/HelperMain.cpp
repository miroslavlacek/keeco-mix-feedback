// MixSightHelper – small command-line helper used by the installer.
//   MixSightHelper.exe --setup-ai    register MixSightBridge.exe in Antigravity as "mixsight"
//                                    and allow its tools (removes the old "keeco-mix-feedback" entry)
//   MixSightHelper.exe --sign-in     open Antigravity in a window for the Google sign-in
//   MixSightHelper.exe --status      print what is set up
#include <juce_core/juce_core.h>
#include <iostream>
#include "AiSetup.h"
#include "AppPaths.h"

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    const auto here = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    const auto bridge = here.getChildFile (mixsight::bridgeExe);
    const auto agy = aisetup::findAgy();

    auto say = [] (const juce::String& s) { std::cout << s.toStdString() << std::endl; };

    if (args.contains ("--status"))
    {
        const auto reg = aisetup::findRegistration();
        say ("agy:        " + (agy.existsAsFile() ? agy.getFullPathName() : juce::String ("not installed")));
        say ("bridge:     " + bridge.getFullPathName() + (bridge.existsAsFile() ? "" : " (missing)"));
        say ("registered: " + (reg.name.isNotEmpty() ? reg.name + " -> " + reg.bridge.getFullPathName() : juce::String ("no")));
        say ("tools ok:   " + juce::String (aisetup::toolsAllowed (mixsight::serverName) ? "yes" : "no"));
        return 0;
    }
    if (args.contains ("--setup-ai"))
    {
        if (! agy.existsAsFile()) { say ("Antigravity CLI (agy) is not installed - skipping AI setup."); return 0; }
        juce::String log, err;
        const bool ok = aisetup::registerBridge (agy, bridge, log);
        say (log);
        const bool allowed = aisetup::allowTools (mixsight::serverName, err);
        if (! allowed) say (err);
        say (ok && allowed ? "AI setup done." : "AI setup incomplete.");
        return ok && allowed ? 0 : 1;
    }
    if (args.contains ("--sign-in"))
    {
        juce::String err;
        if (! aisetup::openSignInWindow (agy, here, err)) { say ("Cannot open Antigravity: " + err); return 1; }
        return 0;
    }
    say ("usage: MixSightHelper --setup-ai | --sign-in | --status");
    return 2;
}
