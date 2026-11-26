// Tests for the MixSight updater.
// usage: kmf_update_tests <served-dir>   with an HTTP server serving <served-dir> and
//        MIXSIGHT_UPDATE_API=http://127.0.0.1:<port>  (the CI starts `python -m http.server`).
#include "Updater.h"
#include <juce_cryptography/juce_cryptography.h>
#include <iostream>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

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

static juce::File makeZip (const juce::File& dest, const juce::String& version)
{
    auto src = dest.getSiblingFile ("zipsrc-" + version);
    src.deleteRecursively();
    auto dll = src.getChildFile ("MixSight.vst3/Contents/x86_64-win/MixSight.vst3");
    dll.getParentDirectory().createDirectory();
    dll.replaceWithText ("plugin " + version);
    src.getChildFile ("MixSightBridge.exe").replaceWithText ("bridge " + version);
    src.getChildFile ("GEMINI.md").replaceWithText ("instructions " + version);

    juce::ZipFile::Builder b;
    for (auto& f : src.findChildFiles (juce::File::findFiles, true))
        b.addFile (f, 9, f.getRelativePathFrom (src).replaceCharacter ('\\', '/'));
    dest.deleteFile();
    juce::FileOutputStream out (dest);
    b.writeToStream (out, nullptr);
    out.flush();
    src.deleteRecursively();
    return dest;
}

static bool waitFor (Updater& u, std::initializer_list<Updater::State> states, int ms = 30000)
{
    for (int t = 0; t < ms; t += 50)
    {
        for (auto s : states) if (u.getState() == s) return true;
        juce::Thread::sleep (50);
    }
    return false;
}

int main (int argc, char** argv)
{
    // --- versions
    CHECK (Updater::compareVersions ("0.5.0", "0.4.2") > 0);
    CHECK (Updater::compareVersions ("v0.5.0", "0.5.0") == 0);
    CHECK (Updater::compareVersions ("0.10.0", "0.9.9") > 0);
    CHECK (Updater::compareVersions ("0.5", "0.5.1") < 0);
    CHECK (Updater::compareVersions ("1.0.0-beta", "0.9.0") > 0);

    // --- release JSON
    {
        const auto json = juce::JSON::parse (R"({"tag_name":"v0.6.0","body":"Novinky","html_url":"https://x/rel",
            "assets":[{"name":"MixSight-Setup-0.6.0.exe","browser_download_url":"https://x/setup.exe"},
                      {"name":"MixSight-update-0.6.0.zip","browser_download_url":"https://x/u.zip"},
                      {"name":"MixSight-update-0.6.0.zip.sha256","browser_download_url":"https://x/u.zip.sha256"}]})");
        Updater::Release r;
        CHECK (Updater::parseRelease (json, r));
        CHECK (r.version == "0.6.0" && r.notes == "Novinky");
        CHECK (r.zipUrl == "https://x/u.zip" && r.shaUrl == "https://x/u.zip.sha256" && r.setupUrl == "https://x/setup.exe");
        Updater::Release none;
        CHECK (! Updater::parseRelease (juce::JSON::parse (R"({"tag_name":"v0.6.0","assets":[]})"), none));
    }

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mixsight_update_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
    tmp.createDirectory();

    // --- unpack over an existing install, including a file Windows keeps locked
    {
        auto install = tmp.getChildFile ("install");
        auto dll = install.getChildFile ("MixSight.vst3/Contents/x86_64-win/MixSight.vst3");
        dll.getParentDirectory().createDirectory();
        install.getChildFile ("MixSightBridge.exe").replaceWithText ("bridge old");
        juce::String err;
       #if JUCE_WINDOWS
        // A real loaded DLL cannot be deleted or overwritten, only renamed – exactly the FL Studio situation.
        juce::File ("C:\\Windows\\System32\\version.dll").copyFileTo (dll);
        HMODULE h = LoadLibraryW (dll.getFullPathName().toWideCharPointer());
        CHECK (h != nullptr);
        CHECK (! dll.deleteFile());
       #else
        dll.replaceWithText ("plugin old");
       #endif
        const auto zip = makeZip (tmp.getChildFile ("u.zip"), "0.6.0");
        CHECK (Updater::installZip (zip, install, err));
        if (err.isNotEmpty()) std::cerr << "installZip: " << err << "\n";
        CHECK (dll.loadFileAsString() == "plugin 0.6.0");
        CHECK (install.getChildFile ("MixSightBridge.exe").loadFileAsString() == "bridge 0.6.0");
        CHECK (install.getChildFile ("GEMINI.md").loadFileAsString() == "instructions 0.6.0");
       #if JUCE_WINDOWS
        CHECK (dll.getSiblingFile ("MixSight.vst3.old-1").existsAsFile());   // the locked one was moved aside
        FreeLibrary (h);
       #endif
        Updater::cleanupOldFiles (install);
        CHECK (install.findChildFiles (juce::File::findFiles, true, "*.old-*").isEmpty());
        CHECK (Updater::isWritable (install));
    }

    // --- full flow against the local HTTP server
    const auto api = juce::SystemStats::getEnvironmentVariable ("MIXSIGHT_UPDATE_API", {});
    if (argc > 1 && api.isNotEmpty())
    {
        const juce::File served (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        auto dl = served.getChildFile ("dl"); dl.createDirectory();
        const auto zip = makeZip (dl.getChildFile ("MixSight-update-0.6.0.zip"), "0.6.0");
        dl.getChildFile ("MixSight-update-0.6.0.zip.sha256").replaceWithText (juce::SHA256 (zip).toHexString() + "  MixSight-update-0.6.0.zip\n");
        auto latest = served.getChildFile ("repos/miroslavlacek/keeco-mix-feedback/releases/latest");
        latest.getParentDirectory().createDirectory();
        latest.replaceWithText (R"({"tag_name":"v0.6.0","body":"Test release","assets":[
            {"name":"MixSight-update-0.6.0.zip","browser_download_url":")" + api + R"(/dl/MixSight-update-0.6.0.zip"},
            {"name":"MixSight-update-0.6.0.zip.sha256","browser_download_url":")" + api + R"(/dl/MixSight-update-0.6.0.zip.sha256"}]})");

        auto install = tmp.getChildFile ("install2");
        install.createDirectory();
        install.getChildFile ("GEMINI.md").replaceWithText ("old");
        setEnv ("MIXSIGHT_INSTALL_DIR", install.getFullPathName());

        Updater u;
        u.checkIfDue();
        CHECK (waitFor (u, { Updater::State::available, Updater::State::upToDate, Updater::State::idle, Updater::State::failed }));
        std::cout << "state after check: " << (int) u.getState() << ", version " << u.getRelease().version << "\n";
        CHECK (u.getState() == Updater::State::available);
        CHECK (u.getRelease().notes == "Test release");

        u.downloadAndInstall();
        CHECK (waitFor (u, { Updater::State::installed, Updater::State::failed }));
        if (u.getState() == Updater::State::failed) std::cerr << "install failed: " << u.getError() << "\n";
        CHECK (u.getState() == Updater::State::installed);
        CHECK (install.getChildFile ("GEMINI.md").loadFileAsString() == "instructions 0.6.0");
        CHECK (install.getChildFile ("MixSight.vst3/Contents/x86_64-win/MixSight.vst3").loadFileAsString() == "plugin 0.6.0");
        CHECK (u.getInstalledNote().isEmpty());          // DAW not restarted yet: running version is still 0.5.0

        // A corrupted package must not be installed.
        dl.getChildFile ("MixSight-update-0.6.0.zip.sha256").replaceWithText (juce::String::repeatedString ("0", 64));
        install.getChildFile ("GEMINI.md").replaceWithText ("old");
        Updater u2;
        u2.checkNow();
        CHECK (waitFor (u2, { Updater::State::available }));
        u2.downloadAndInstall();
        CHECK (waitFor (u2, { Updater::State::installed, Updater::State::failed }));
        CHECK (u2.getState() == Updater::State::failed);
        CHECK (u2.getError().contains ("kontrol"));
        CHECK (install.getChildFile ("GEMINI.md").loadFileAsString() == "old");
    }
    else
        std::cout << "(skipping HTTP test: no server)\n";

    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "update tests: all passed\n" : "update tests: FAILED\n");
    return failures == 0 ? 0 : 1;
}
