#pragma once
// Runs a child process without a console window, stdin = NUL, and streams its
// stdout line by line. stderr is collected separately. Used to run the
// Antigravity CLI (agy) in headless mode from inside the plugin.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <signal.h>
 #include <sys/wait.h>
 #include <unistd.h>
#endif

namespace kmf
{
class ProcessRunner
{
public:
    using LineCallback = std::function<void (const std::string&)>;

    ~ProcessRunner() { kill(); }

    // Blocks until the process exits (call it from a worker thread). args[0] is the program.
    // Returns the exit code, or -1 if the process could not be started (see getError()).
    int run (const std::vector<std::string>& args, const std::string& workingDir, const LineCallback& onStdoutLine)
    {
        errorText.clear(); stderrText.clear();
        killed = false;
       #if defined (_WIN32)
        return runWindows (args, workingDir, onStdoutLine);
       #else
        return runPosix (args, workingDir, onStdoutLine);
       #endif
    }

    void kill()
    {
        killed = true;
        std::lock_guard<std::mutex> l (handleLock);
       #if defined (_WIN32)
        if (processHandle != nullptr) TerminateProcess (processHandle, 1);
       #else
        if (pid > 0) ::kill (pid, SIGKILL);
       #endif
    }

    // Starts a program in its own visible console window and returns immediately
    // (used for the Google sign-in and the Antigravity installer).
    static bool launchInNewConsole (const std::vector<std::string>& args, const std::string& workingDir, std::string& error)
    {
       #if defined (_WIN32)
        std::wstring cmd;
        for (size_t i = 0; i < args.size(); ++i) { if (i) cmd += L' '; cmd += quoteArg (widen (args[i])); }
        std::vector<wchar_t> cmdBuf (cmd.begin(), cmd.end()); cmdBuf.push_back (0);
        const std::wstring app = widen (args[0]), cwd = widen (workingDir);
        STARTUPINFOW si {}; si.cb = sizeof (si);
        PROCESS_INFORMATION pi {};
        if (! CreateProcessW (app.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
                              nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        {
            error = "CreateProcess failed (error " + std::to_string (GetLastError()) + ")";
            return false;
        }
        CloseHandle (pi.hThread); CloseHandle (pi.hProcess);
        return true;
       #else
        const pid_t child = fork();
        if (child < 0) { error = "fork failed"; return false; }
        if (child == 0)
        {
            setsid();
            if (fork() != 0) _exit (0);                       // double fork: no zombie
            if (! workingDir.empty() && chdir (workingDir.c_str()) != 0) _exit (126);
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back (const_cast<char*> (a.c_str()));
            argv.push_back (nullptr);
            execv (args[0].c_str(), argv.data());
            _exit (127);
        }
        int st = 0; waitpid (child, &st, 0);
        return true;
       #endif
    }

    bool wasKilled() const noexcept { return killed; }
    std::string getError() const { return errorText; }
    std::string getStderr() const { std::lock_guard<std::mutex> l (stderrLock); return stderrText; }

   #if defined (_WIN32)
    static std::wstring widen (const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
        std::wstring w ((size_t) n, L'\0');
        MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), &w[0], n);
        return w;
    }

    // Quoting compatible with CommandLineToArgvW / the MSVC runtime / Go.
    static std::wstring quoteArg (const std::wstring& a)
    {
        if (! a.empty() && a.find_first_of (L" \t\n\v\"") == std::wstring::npos) return a;
        std::wstring out = L"\"";
        size_t backslashes = 0;
        for (wchar_t c : a)
        {
            if (c == L'\\') { ++backslashes; continue; }
            if (c == L'"') { out.append (backslashes * 2 + 1, L'\\'); out.push_back (L'"'); }
            else           { out.append (backslashes, L'\\');         out.push_back (c); }
            backslashes = 0;
        }
        out.append (backslashes * 2, L'\\');
        out.push_back (L'"');
        return out;
    }
   #endif

private:
    void appendStderr (const char* data, size_t n)
    {
        std::lock_guard<std::mutex> l (stderrLock);
        stderrText.append (data, n);
        if (stderrText.size() > 64 * 1024) stderrText.erase (0, stderrText.size() - 64 * 1024);
    }

    static void feedLines (std::string& pending, const char* data, size_t n, const LineCallback& cb)
    {
        pending.append (data, n);
        size_t pos;
        while ((pos = pending.find ('\n')) != std::string::npos)
        {
            std::string line = pending.substr (0, pos);
            pending.erase (0, pos + 1);
            if (! line.empty() && line.back() == '\r') line.pop_back();
            if (cb) cb (line);
        }
    }

   #if defined (_WIN32)
    int runWindows (const std::vector<std::string>& args, const std::string& workingDir, const LineCallback& cb)
    {
        SECURITY_ATTRIBUTES sa { sizeof (sa), nullptr, TRUE };
        HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
        if (! CreatePipe (&outRead, &outWrite, &sa, 0) || ! CreatePipe (&errRead, &errWrite, &sa, 0))
            { errorText = "CreatePipe failed"; return -1; }
        SetHandleInformation (outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation (errRead, HANDLE_FLAG_INHERIT, 0);
        HANDLE nulIn = CreateFileW (L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

        std::wstring cmd;
        for (size_t i = 0; i < args.size(); ++i) { if (i) cmd += L' '; cmd += quoteArg (widen (args[i])); }
        std::vector<wchar_t> cmdBuf (cmd.begin(), cmd.end()); cmdBuf.push_back (0);
        const std::wstring app = widen (args[0]), cwd = widen (workingDir);

        STARTUPINFOW si {}; si.cb = sizeof (si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nulIn; si.hStdOutput = outWrite; si.hStdError = errWrite;
        PROCESS_INFORMATION pi {};

        const BOOL ok = CreateProcessW (app.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
        CloseHandle (outWrite); CloseHandle (errWrite);
        if (nulIn != INVALID_HANDLE_VALUE) CloseHandle (nulIn);
        if (! ok)
        {
            errorText = "CreateProcess failed (error " + std::to_string (GetLastError()) + ")";
            CloseHandle (outRead); CloseHandle (errRead);
            return -1;
        }
        { std::lock_guard<std::mutex> l (handleLock); processHandle = pi.hProcess; }
        CloseHandle (pi.hThread);
        if (killed) TerminateProcess (pi.hProcess, 1);

        std::thread errThread ([this, errRead]
        {
            char buf[4096]; DWORD got = 0;
            while (ReadFile (errRead, buf, sizeof (buf), &got, nullptr) && got > 0) appendStderr (buf, got);
        });

        std::string pending; char buf[8192]; DWORD got = 0;
        while (ReadFile (outRead, buf, sizeof (buf), &got, nullptr) && got > 0)
            feedLines (pending, buf, got, cb);
        if (! pending.empty() && cb) cb (pending);

        errThread.join();
        WaitForSingleObject (pi.hProcess, INFINITE);
        DWORD code = 1; GetExitCodeProcess (pi.hProcess, &code);
        { std::lock_guard<std::mutex> l (handleLock); processHandle = nullptr; }
        CloseHandle (pi.hProcess); CloseHandle (outRead); CloseHandle (errRead);
        return (int) code;
    }

    HANDLE processHandle = nullptr;
   #else
    int runPosix (const std::vector<std::string>& args, const std::string& workingDir, const LineCallback& cb)
    {
        int outPipe[2], errPipe[2];
        if (pipe (outPipe) != 0 || pipe (errPipe) != 0) { errorText = "pipe failed"; return -1; }
        const pid_t child = fork();
        if (child < 0) { errorText = "fork failed"; return -1; }
        if (child == 0)
        {
            const int nul = open ("/dev/null", O_RDONLY);
            dup2 (nul, 0); dup2 (outPipe[1], 1); dup2 (errPipe[1], 2);
            close (outPipe[0]); close (errPipe[0]);
            if (! workingDir.empty() && chdir (workingDir.c_str()) != 0) _exit (126);
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back (const_cast<char*> (a.c_str()));
            argv.push_back (nullptr);
            execv (args[0].c_str(), argv.data());
            _exit (127);
        }
        close (outPipe[1]); close (errPipe[1]);
        { std::lock_guard<std::mutex> l (handleLock); pid = child; }
        if (killed) ::kill (child, SIGKILL);

        std::thread errThread ([this, fd = errPipe[0]]
        {
            char buf[4096]; ssize_t got;
            while ((got = read (fd, buf, sizeof (buf))) > 0) appendStderr (buf, (size_t) got);
        });
        std::string pending; char buf[8192]; ssize_t got;
        while ((got = read (outPipe[0], buf, sizeof (buf))) > 0) feedLines (pending, buf, (size_t) got, cb);
        if (! pending.empty() && cb) cb (pending);
        errThread.join();
        close (outPipe[0]); close (errPipe[0]);

        int status = 0; waitpid (child, &status, 0);
        { std::lock_guard<std::mutex> l (handleLock); pid = -1; }
        if (WIFEXITED (status)) { const int c = WEXITSTATUS (status); if (c == 127) errorText = "exec failed"; return c == 127 ? -1 : c; }
        return 1;
    }

    pid_t pid = -1;
   #endif

    std::atomic<bool> killed { false };
    std::mutex handleLock;
    mutable std::mutex stderrLock;
    std::string stderrText, errorText;
};
} // namespace kmf
