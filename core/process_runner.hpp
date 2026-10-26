#ifndef PGR_PROCESS_RUNNER_HPP
#define PGR_PROCESS_RUNNER_HPP

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// Invokes the executable directly. No shell expansion or metacharacter evaluation.
namespace pgr_process {
inline std::string display_arg(const std::string& s) {
    if (s.find_first_of(" \t\"'\\;&|<>$") == std::string::npos) return s;
    std::string r = "\"";
    for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
    return r + "\"";
}
inline std::string display(const std::vector<std::string>& args) {
    std::string result;
    for (const auto& a : args) { if (!result.empty()) result += ' '; result += display_arg(a); }
    return result;
}
#ifdef _WIN32
inline std::wstring utf16(const std::string& str) {
    if (str.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);
    if (n == 0) return {};
    std::wstring buf(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), -1, &buf[0], n);
    buf.pop_back();
    return buf;
}
inline std::wstring win_quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') { result.append(backslashes * 2 + 1, L'\\'); result += L'"'; backslashes = 0; continue; }
        result.append(backslashes, L'\\'); backslashes = 0; result += c;
    }
    result.append(backslashes * 2, L'\\');
    return result + L'"';
}
#endif
inline void relay(const char* data, size_t size, bool quiet,
                  const std::function<void(const std::string&)>& callback,
                  std::string& pending) {
    if (!quiet) { std::cout.write(data, static_cast<std::streamsize>(size)); std::cout.flush(); }
    if (!callback) return;
    pending.append(data, size);
    size_t end;
    while ((end = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        callback(line);
        pending.erase(0, end + 1);
    }
}
inline int run(const std::vector<std::string>& args, bool quiet = false,
               const std::function<void(const std::string&)>& callback = {}) {
    if (args.empty() || args[0].empty()) return -1;
#ifdef _WIN32
    std::wstring cmd;
    for (const auto& arg : args) {
        auto wide = utf16(arg);
        if (wide.empty() && !arg.empty()) return -1;
        if (!cmd.empty()) cmd += L' ';
        cmd += win_quote(wide);
    }
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const bool capture = quiet || static_cast<bool>(callback);
    HANDLE hread = INVALID_HANDLE_VALUE, hwrite = INVALID_HANDLE_VALUE;
    if (capture) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        if (!CreatePipe(&hread, &hwrite, &sa, 0)) return -1;
        SetHandleInformation(hread, HANDLE_FLAG_INHERIT, 0);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hwrite;
        si.hStdError = hwrite;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &si, &pi)) {
        if (capture) { CloseHandle(hread); CloseHandle(hwrite); }
        if (!quiet) std::cerr << "Process start failed (Win32): " << GetLastError() << '\n';
        return -1;
    }
    if (capture) {
        CloseHandle(hwrite);
        char buf[4096];
        DWORD n;
        std::string pending;
        while (ReadFile(hread, buf, sizeof(buf), &n, nullptr) && n > 0)
            relay(buf, n, quiet, callback, pending);
        if (!pending.empty() && callback) callback(pending);
        CloseHandle(hread);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    const bool capture = static_cast<bool>(callback);
    int fds[2] = {-1, -1};
    if (capture && pipe(fds) != 0) { if (!quiet) std::perror("pipe"); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        if (capture) { close(fds[0]); close(fds[1]); }
        if (!quiet) std::perror("fork");
        return -1;
    }
    if (pid == 0) {
        if (capture) {
            close(fds[0]);
            dup2(fds[1], STDOUT_FILENO);
            dup2(fds[1], STDERR_FILENO);
            close(fds[1]);
        } else if (quiet) {
            std::freopen("/dev/null", "w", stdout);
            std::freopen("/dev/null", "w", stderr);
        }
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        std::perror(argv[0]);
        _exit(127);
    }
    if (capture) {
        close(fds[1]);
        char buf[4096];
        std::string pending;
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof(buf))) > 0)
            relay(buf, static_cast<size_t>(n), quiet, callback, pending);
        if (!pending.empty()) callback(pending);
        close(fds[0]);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) { if (!quiet) std::perror("waitpid"); return -1; }
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}
inline bool available(const std::string& executable) {
    if (executable.empty()) return false;
    // CMake/GCC support a version switch. No shell and no PATH parsing.
    return run({executable, "--version"}, true) == 0;
}
}
#endif
