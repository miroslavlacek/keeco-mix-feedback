#include "AiSetup.h"
#include "AppPaths.h"
#include "ProcessRunner.h"
#if JUCE_WINDOWS
 #include <wincred.h>
 #pragma comment (lib, "advapi32.lib")
#endif

namespace aisetup
{
static std::vector<std::string> toStd (const juce::StringArray& a)
{
    std::vector<std::string> v;
    for (auto& s : a) v.push_back (s.toStdString());
    return v;
}

juce::File agyHome()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("KMF_AGY_HOME", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".gemini");
}

juce::File mcpConfigFile() { return agyHome().getChildFile ("config").getChildFile ("mcp_config.json"); }
juce::File settingsFile()  { return agyHome().getChildFile ("antigravity-cli").getChildFile ("settings.json"); }

juce::File findAgy()
{
    const auto envExe = juce::SystemStats::getEnvironmentVariable ("KMF_AGY_PATH", {});
    if (envExe.isNotEmpty()) return juce::File (envExe).existsAsFile() ? juce::File (envExe) : juce::File();
   #if JUCE_WINDOWS
    const auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
    if (local.isNotEmpty() && juce::File (local).getChildFile ("agy\\bin\\agy.exe").existsAsFile())
        return juce::File (local).getChildFile ("agy\\bin\\agy.exe");
    const juce::String exeName = "agy.exe"; const char* sep = ";";
   #else
    const juce::String exeName = "agy"; const char* sep = ":";
   #endif
    juce::StringArray dirs;
    dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), sep, {});
    for (auto& d : dirs)
        if (d.isNotEmpty() && juce::File::isAbsolutePath (d) && juce::File (d).getChildFile (exeName).existsAsFile())
            return juce::File (d).getChildFile (exeName);
    return {};
}

Registration findRegistration()
{
    Registration best;
    const auto cfg = juce::JSON::parse (mcpConfigFile());
    if (auto* servers = cfg.getProperty ("mcpServers", {}).getDynamicObject())
        for (auto& p : servers->getProperties())
        {
            const auto cmd = p.value.getProperty ("command", {}).toString();
            if (! (cmd.containsIgnoreCase ("MixSightBridge") || cmd.containsIgnoreCase ("KeecoMcpBridge"))) continue;
            Registration r { p.name.toString(), juce::File::isAbsolutePath (cmd) ? juce::File (cmd) : juce::File() };
            // prefer the current name and an existing file
            if (best.name.isEmpty() || (r.name == mixsight::serverName && r.bridge.existsAsFile()))
                best = r;
        }
    return best;
}

int runAndCapture (const juce::StringArray& args, const juce::File& cwd, juce::String& output)
{
    kmf::ProcessRunner pr;
    std::string out;
    const int code = pr.run (toStd (args), cwd.getFullPathName().toStdString(),
                             [&out] (const std::string& l) { out += l + "\n"; });
    output = juce::String::fromUTF8 ((out + pr.getStderr() + pr.getError()).c_str()).trim();
    return code;
}

bool registerBridge (const juce::File& agy, const juce::File& bridge, juce::String& log)
{
    if (! agy.existsAsFile()) { log = "agy not found"; return false; }
    if (! bridge.existsAsFile()) { log = "bridge not found: " + bridge.getFullPathName(); return false; }
    juce::String out;
    for (auto old : { "keeco-mix-feedback", mixsight::serverName })
        runAndCapture ({ agy.getFullPathName(), "mcp", "remove", old }, bridge.getParentDirectory(), out);
    const int code = runAndCapture ({ agy.getFullPathName(), "mcp", "add", mixsight::serverName, bridge.getFullPathName() },
                                    bridge.getParentDirectory(), out);
    log = out;
    const auto reg = findRegistration();
    return code == 0 && reg.name == mixsight::serverName && reg.bridge == bridge;
}

bool toolsAllowed (const juce::String& serverName)
{
    const auto rule = "mcp(" + serverName + "/*)";
    const auto settings = juce::JSON::parse (settingsFile());
    if (settings.getProperty ("toolPermission", {}).toString() == "always-proceed") return true;
    if (auto* allow = settings.getProperty ("permissions", {}).getProperty ("allow", {}).getArray())
        for (auto& v : *allow)
            if (v.toString() == rule) return true;
    return false;
}

bool allowTools (const juce::String& serverName, juce::String& error)
{
    const auto file = settingsFile();
    juce::var root;
    if (file.existsAsFile() && file.loadFileAsString().trim().isNotEmpty())
    {
        const auto res = juce::JSON::parse (file.loadFileAsString(), root);
        if (res.failed() || root.getDynamicObject() == nullptr)
            { error = juce::String::fromUTF8 ("settings.json sa nedá prečítať (") + res.getErrorMessage() + juce::String::fromUTF8 ("), nemením ho."); return false; }
    }
    else
        root = juce::var (new juce::DynamicObject());

    auto* obj = root.getDynamicObject();
    juce::var perms = obj->getProperty ("permissions");
    if (perms.getDynamicObject() == nullptr) { perms = juce::var (new juce::DynamicObject()); obj->setProperty ("permissions", perms); }
    juce::var allow = perms.getProperty ("allow", {});
    if (! allow.isArray()) allow = juce::var (juce::Array<juce::var>());
    const auto rule = "mcp(" + serverName + "/*)";
    bool present = false;
    for (auto& v : *allow.getArray()) present = present || v.toString() == rule;
    if (! present) allow.getArray()->add (rule);
    perms.getDynamicObject()->setProperty ("allow", allow);

    file.getParentDirectory().createDirectory();
    if (file.existsAsFile()) file.copyFileTo (file.getSiblingFile ("settings.json.bak"));
    if (! file.replaceWithText (juce::JSON::toString (root)))
        { error = juce::String::fromUTF8 ("Nepodarilo sa zapísať ") + file.getFullPathName(); return false; }
    return true;
}

bool openSignInWindow (const juce::File& agy, const juce::File& workspace, juce::String& error)
{
    if (! agy.existsAsFile()) { error = "agy not found"; return false; }
    std::string err;
    const bool ok = kmf::ProcessRunner::launchInNewConsole ({ agy.getFullPathName().toStdString() },
                                                            workspace.getFullPathName().toStdString(), err);
    error = err;
    return ok;
}

bool isSignedIn()
{
    const auto fake = juce::SystemStats::getEnvironmentVariable ("KMF_FAKE_SIGNED_IN_FILE", {});   // tests
    if (fake.isNotEmpty()) return juce::File (fake).existsAsFile();
   #if JUCE_WINDOWS
    PCREDENTIALW cred = nullptr;
    if (CredReadW (L"gemini:antigravity", CRED_TYPE_GENERIC, 0, &cred))
    {
        CredFree (cred);
        return true;
    }
    return false;
   #else
    return true;      // sign-in detection is Windows-only
   #endif
}

bool signInAndWait (const juce::File& agy, const juce::File& workspace, int timeoutSeconds, juce::String& error,
                    const std::function<bool()>& shouldStop)
{
    if (isSignedIn()) return true;
    if (! agy.existsAsFile()) { error = "agy not found"; return false; }

   #if JUCE_WINDOWS
    // Visible window (the user picks "Login with Google" there), inside a job object so the
    // whole agy process tree can be closed once the sign-in is stored.
    HANDLE job = CreateJobObjectW (nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li {};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job != nullptr) SetInformationJobObject (job, JobObjectExtendedLimitInformation, &li, sizeof (li));

    std::wstring cmd = kmf::ProcessRunner::quoteArg (agy.getFullPathName().toWideCharPointer());
    std::vector<wchar_t> cmdBuf (cmd.begin(), cmd.end()); cmdBuf.push_back (0);
    const std::wstring cwd = workspace.getFullPathName().toWideCharPointer();
    STARTUPINFOW si {}; si.cb = sizeof (si);
    PROCESS_INFORMATION pi {};
    if (! CreateProcessW (agy.getFullPathName().toWideCharPointer(), cmdBuf.data(), nullptr, nullptr, FALSE,
                          CREATE_NEW_CONSOLE | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
                          workspace.isDirectory() ? cwd.c_str() : nullptr, &si, &pi))
    {
        error = "CreateProcess failed (" + juce::String ((int) GetLastError()) + ")";
        if (job != nullptr) CloseHandle (job);
        return false;
    }
    if (job != nullptr) AssignProcessToJobObject (job, pi.hProcess);
    ResumeThread (pi.hThread);
    CloseHandle (pi.hThread);

    bool ok = false;
    for (int i = 0; i < timeoutSeconds * 2; ++i)
    {
        if (isSignedIn()) { ok = true; juce::Thread::sleep (2500); break; }       // let agy finish writing
        if (WaitForSingleObject (pi.hProcess, 500) == WAIT_OBJECT_0) { ok = isSignedIn(); break; }   // user closed it
        if (shouldStop && shouldStop()) break;
    }
    if (job != nullptr) { TerminateJobObject (job, 0); CloseHandle (job); }
    else TerminateProcess (pi.hProcess, 0);
    CloseHandle (pi.hProcess);
    if (! ok && error.isEmpty()) error = juce::String::fromUTF8 ("Prihlásenie sa nedokončilo.");
    return ok;
   #else
    juce::ignoreUnused (workspace, timeoutSeconds, shouldStop);
    error = "sign-in window is only supported on Windows";
    return false;
   #endif
}

bool openAgyInstaller (juce::String& error)
{
   #if JUCE_WINDOWS
    const auto sysRoot = juce::SystemStats::getEnvironmentVariable ("SystemRoot", "C:\\Windows");
    const auto ps = juce::File (sysRoot).getChildFile ("System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    std::string err;
    const bool ok = kmf::ProcessRunner::launchInNewConsole (
        { ps.getFullPathName().toStdString(), "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
          "irm https://antigravity.google/cli/install.ps1 | iex; Write-Host ''; Write-Host 'Hotovo - toto okno mozes zavriet.'; Read-Host" },
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString(), err);
    error = err;
    return ok;
   #else
    error = "Antigravity installer is only scripted on Windows";
    return false;
   #endif
}
} // namespace aisetup
