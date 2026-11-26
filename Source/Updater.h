#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

// Checks GitHub for a newer MixSight release, downloads the update package
// (MixSight-update-<ver>.zip + .sha256), verifies it and unpacks it over the
// install folder. Files that Windows keeps locked (the loaded plugin DLL, a running
// bridge) are renamed to *.old-N first, which Windows allows; the new version is
// used after the DAW restarts. One instance per process (SharedResourcePointer).
class Updater : private juce::Thread
{
public:
    enum class State { idle, checking, upToDate, available, downloading, installing, installed, failed };

    struct Release
    {
        juce::String version, notes, zipUrl, shaUrl, setupUrl, pageUrl;
    };

    Updater();
    ~Updater() override;

    void checkIfDue (double minHoursBetweenChecks = 6.0);   // message thread; async
    void checkNow();
    void downloadAndInstall();                              // after the user confirmed

    State getState() const noexcept { return state.load(); }
    float getProgress() const noexcept { return progress.load(); }
    Release getRelease() const;
    juce::String getError() const;
    juce::String getInstalledNote();                        // "Aktualizované na x" once after an update

    static juce::String currentVersion();
    static int compareVersions (const juce::String& a, const juce::String& b);   // <0, 0, >0
    static juce::String apiBase();                          // https://api.github.com (MIXSIGHT_UPDATE_API overrides)
    static bool parseRelease (const juce::var& json, Release& out);
    static bool installZip (const juce::File& zip, const juce::File& targetDir, juce::String& error);
    static void cleanupOldFiles (const juce::File& dir);
    static bool isWritable (const juce::File& dir);

private:
    void run() override;
    bool doCheck();
    bool doInstall();
    bool download (const juce::String& url, const juce::File& dest, bool reportProgress, juce::String& error);
    void fail (const juce::String& e);

    enum class Job { none, check, install };
    std::atomic<Job> job { Job::none };
    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    mutable juce::CriticalSection lock;
    Release release;
    juce::String error;
};
