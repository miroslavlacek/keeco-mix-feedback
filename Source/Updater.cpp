#include "Updater.h"
#include "AppPaths.h"
#include <juce_cryptography/juce_cryptography.h>

namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

juce::File updatesDir()   { auto d = mixsight::appData().getChildFile ("Updates"); d.createDirectory(); return d; }
juce::File lastCheckFile(){ return mixsight::appData().getChildFile ("update-last-check.txt"); }
juce::File markerFile()   { return mixsight::appData().getChildFile ("update-installed.txt"); }

juce::URL::InputStreamOptions httpOptions (int timeoutMs)
{
    return juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
        .withExtraHeaders ("User-Agent: MixSight/" + mixsight::versionString() + "\r\nAccept: application/vnd.github+json")
        .withConnectionTimeoutMs (timeoutMs)
        .withNumRedirectsToFollow (8);
}
}

Updater::Updater() : juce::Thread ("MixSight updater") {}
Updater::~Updater() { stopThread (8000); }

juce::String Updater::currentVersion() { return mixsight::versionString(); }

juce::String Updater::apiBase()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("MIXSIGHT_UPDATE_API", {});
    return env.isNotEmpty() ? env.trimCharactersAtEnd ("/") : juce::String ("https://api.github.com");
}

int Updater::compareVersions (const juce::String& a, const juce::String& b)
{
    auto parts = [] (juce::String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        juce::StringArray p; p.addTokens (v.upToFirstOccurrenceOf ("-", false, false), ".", {});
        while (p.size() < 3) p.add ("0");
        return p;
    };
    const auto pa = parts (a), pb = parts (b);
    for (int i = 0; i < 3; ++i)
        if (pa[i].getIntValue() != pb[i].getIntValue()) return pa[i].getIntValue() < pb[i].getIntValue() ? -1 : 1;
    return 0;
}

bool Updater::parseRelease (const juce::var& json, Release& out)
{
    out = {};
    out.version = json.getProperty ("tag_name", {}).toString().trim().trimCharactersAtStart ("vV");
    out.notes   = json.getProperty ("body", {}).toString().trim();
    out.pageUrl = json.getProperty ("html_url", {}).toString();
    if (auto* assets = json.getProperty ("assets", {}).getArray())
        for (auto& a : *assets)
        {
            const auto name = a.getProperty ("name", {}).toString();
            const auto url  = a.getProperty ("browser_download_url", {}).toString();
            if (name.startsWithIgnoreCase ("MixSight-update-") && name.endsWithIgnoreCase (".zip")) out.zipUrl = url;
            else if (name.startsWithIgnoreCase ("MixSight-update-") && name.endsWithIgnoreCase (".zip.sha256")) out.shaUrl = url;
            else if (name.startsWithIgnoreCase ("MixSight-Setup") && name.endsWithIgnoreCase (".exe")) out.setupUrl = url;
        }
    return out.version.isNotEmpty() && out.zipUrl.isNotEmpty() && out.shaUrl.isNotEmpty();
}

bool Updater::isWritable (const juce::File& dir)
{
    if (! dir.isDirectory()) return false;
    auto probe = dir.getChildFile (".mixsight-write-test-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
    const bool ok = probe.replaceWithText ("x");
    probe.deleteFile();
    return ok;
}

void Updater::cleanupOldFiles (const juce::File& dir)
{
    if (! dir.isDirectory()) return;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.old-*"))
        f.deleteFile();                       // still-locked ones stay until the next start
}

bool Updater::installZip (const juce::File& zipFile, const juce::File& targetDir, juce::String& err)
{
    juce::ZipFile zip (zipFile);
    if (zip.getNumEntries() == 0) { err = u8 ("Balík aktualizácie je prázdny alebo poškodený."); return false; }
    if (! targetDir.createDirectory()) { err = u8 ("Priečinok ") + targetDir.getFullPathName() + u8 (" sa nedá vytvoriť."); return false; }

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        const auto name = e->filename.replaceCharacter ('\\', '/');
        if (name.endsWith ("/") || name.isEmpty()) continue;
        if (name.contains ("..") || juce::File::isAbsolutePath (name)) { err = "Unsafe path in update: " + name; return false; }

        const auto target = targetDir.getChildFile (name);
        target.getParentDirectory().createDirectory();
        if (target.exists() && ! target.deleteFile())
        {
            // Locked (plugin DLL loaded by the DAW, bridge running): rename it out of the way.
            juce::File old;
            for (int n = 1; n < 1000; ++n)
                if (! (old = target.getSiblingFile (target.getFileName() + ".old-" + juce::String (n))).exists()) break;
            if (! target.moveFileTo (old)) { err = u8 ("Súbor sa nedá nahradiť: ") + target.getFullPathName(); return false; }
        }
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        juce::FileOutputStream out (target);
        if (in == nullptr || ! out.openedOk()) { err = u8 ("Súbor sa nedá zapísať: ") + target.getFullPathName(); return false; }
        out.writeFromInputStream (*in, -1);
        out.flush();
        if (out.getStatus().failed()) { err = out.getStatus().getErrorMessage(); return false; }
    }
    return true;
}

// ---------------------------------------------------------------- public (message thread)

void Updater::checkIfDue (double)
{
    // Once per DAW session (shared by all plugin instances); GitHub allows 60 anonymous checks per hour.
    if (state.load() == State::idle) checkNow();
}

void Updater::checkNow()
{
    const auto s = state.load();
    if (isThreadRunning() || s == State::downloading || s == State::installing || s == State::installed) return;
    job = Job::check;
    state = State::checking;
    startThread();
}

void Updater::downloadAndInstall()
{
    if (isThreadRunning() || state.load() != State::available) return;
    job = Job::install;
    state = State::downloading;
    progress = 0.0f;
    startThread();
}

Updater::Release Updater::getRelease() const { const juce::ScopedLock sl (lock); return release; }
juce::String Updater::getError() const       { const juce::ScopedLock sl (lock); return error; }

juce::String Updater::getInstalledNote()
{
    const auto marker = markerFile();
    if (! marker.existsAsFile()) return {};
    const auto v = marker.loadFileAsString().trim();
    if (compareVersions (v, currentVersion()) > 0) return {};      // new files are there, DAW not restarted yet
    marker.deleteFile();
    return u8 ("MixSight bol aktualizovaný na verziu ") + currentVersion() + ".";
}

void Updater::fail (const juce::String& e)
{
    { const juce::ScopedLock sl (lock); error = e; }
    state = State::failed;
}

// ---------------------------------------------------------------- worker

void Updater::run()
{
    if (job == Job::check)        doCheck();
    else if (job == Job::install) doInstall();
    job = Job::none;
}

bool Updater::doCheck()
{
    const juce::URL url (apiBase() + "/repos/" + mixsight::githubRepo + "/releases/latest");
    int status = 0;
    auto stream = url.createInputStream (httpOptions (10000).withStatusCode (&status));
    if (stream == nullptr || status >= 400)
    {
        // No network / no release yet: stay quiet, try again next time.
        state = status == 404 ? State::upToDate : State::idle;
        return false;
    }
    const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    Release r;
    if (! parseRelease (json, r)) { state = State::upToDate; return false; }

    lastCheckFile().replaceWithText (juce::String (juce::Time::currentTimeMillis()));
    { const juce::ScopedLock sl (lock); release = r; }
    state = compareVersions (r.version, currentVersion()) > 0 ? State::available : State::upToDate;
    return true;
}

bool Updater::download (const juce::String& urlText, const juce::File& dest, bool reportProgress, juce::String& err)
{
    int status = 0;
    auto stream = juce::URL (urlText).createInputStream (httpOptions (20000).withStatusCode (&status));
    if (stream == nullptr || status >= 400) { err = u8 ("Sťahovanie zlyhalo (") + juce::String (status) + "): " + urlText; return false; }
    dest.deleteFile();
    juce::FileOutputStream out (dest);
    if (! out.openedOk()) { err = u8 ("Nedá sa zapísať ") + dest.getFullPathName(); return false; }
    const auto total = stream->getTotalLength();
    juce::HeapBlock<char> buf (65536);
    juce::int64 done = 0;
    while (! stream->isExhausted() && ! threadShouldExit())
    {
        const int n = stream->read (buf, 65536);
        if (n <= 0) break;
        out.write (buf, (size_t) n);
        done += n;
        if (reportProgress && total > 0) progress = (float) done / (float) total;
    }
    out.flush();
    if (threadShouldExit()) { err = "cancelled"; return false; }
    if (total > 0 && done != total) { err = u8 ("Sťahovanie sa prerušilo."); return false; }
    return true;
}

bool Updater::doInstall()
{
    const auto r = getRelease();
    juce::String err;
    const auto zip = updatesDir().getChildFile ("MixSight-update-" + r.version + ".zip");
    const auto sha = updatesDir().getChildFile ("MixSight-update-" + r.version + ".zip.sha256");
    if (! download (r.shaUrl, sha, false, err) || ! download (r.zipUrl, zip, true, err)) { fail (err); return false; }

    const auto expected = sha.loadFileAsString().trim().upToFirstOccurrenceOf (" ", false, false).toLowerCase();
    const auto actual = juce::SHA256 (zip).toHexString().toLowerCase();
    if (expected.length() != 64 || expected != actual)
    {
        fail (u8 ("Stiahnutý balík nesedí s kontrolným súčtom – aktualizácia sa nenainštalovala."));
        return false;
    }

    state = State::installing;
    const auto target = mixsight::installDir();
    if (! isWritable (target))
    {
        // Installed without the installer (no write access): hand over to the setup program, it asks for admin rights.
        if (r.setupUrl.isEmpty()) { fail (u8 ("Do priečinka pluginu sa nedá zapisovať: ") + target.getFullPathName()); return false; }
        const auto setup = updatesDir().getChildFile ("MixSight-Setup-" + r.version + ".exe");
        if (! download (r.setupUrl, setup, true, err)) { fail (err); return false; }
        if (! setup.startAsProcess()) { fail (u8 ("Inštalátor sa nepodarilo spustiť: ") + setup.getFullPathName()); return false; }
    }
    else if (! installZip (zip, target, err)) { fail (err); return false; }

    markerFile().replaceWithText (r.version);
    zip.deleteFile(); sha.deleteFile();
    state = State::installed;
    return true;
}
