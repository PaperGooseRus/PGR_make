#ifndef PGR_PROCESS_RUNNER_HPP
#define PGR_PROCESS_RUNNER_HPP

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
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
#include <csignal>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pgr_process {
struct cancel_token { std::atomic<bool> requested{false}; void cancel() { requested.store(true); } };
struct options {
    bool quiet = false;
    std::filesystem::path working_directory;
    std::vector<std::filesystem::path> extra_path;
    cancel_token* cancel = nullptr;
    std::function<void(const std::string&)> output;
};
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
    if (!n) return {};
    std::wstring s(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), -1, s.data(), n);
    s.pop_back(); return s;
}
inline std::wstring win_quote(const std::wstring& value) {
    std::wstring result = L"\""; size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { result.append(slashes * 2 + 1, L'\\'); result += L'"'; slashes = 0; continue; }
        result.append(slashes, L'\\'); slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
#endif
#if defined(__linux__)
inline void signal_descendants(pid_t root, int sig, int depth = 0) {
    if (depth > 30) return;
    // Some Linux kernels do not expose /proc/PID/task/PID/children.
    // Enumerating PPid from /proc/*/status also covers those kernels.
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/proc",ec), end;
         !ec && it!=end; it.increment(ec)) {
        const auto name=it->path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789")!=std::string::npos) continue;
        int pid=0;
        try { pid=std::stoi(name); } catch (...) {continue;}
        std::ifstream status(it->path()/"status");
        if (!status) continue;
        std::string line;
        pid_t parent=0;
        while (std::getline(status,line)) {
            if (line.rfind("PPid:",0)==0) {
                try {parent=static_cast<pid_t>(std::stoi(line.substr(5)));} catch (...) {}
                break;
            }
        }
        if (parent==root) {
            signal_descendants(pid,sig,depth+1);
            kill(pid,sig);
        }
    }
}
#endif
inline void relay(const char* data, size_t n, const options& opt, std::string& pending) {
    if (!opt.quiet) { std::cout.write(data, static_cast<std::streamsize>(n)); std::cout.flush(); }
    if (!opt.output) return;
    pending.append(data, n); size_t end;
    while ((end = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        opt.output(line); pending.erase(0, end + 1);
    }
}
inline int run_ex(const std::vector<std::string>& args, const options& opt) {
    if (args.empty() || args[0].empty()) return -1;
    if (opt.cancel && opt.cancel->requested.load()) return 130;
#ifdef _WIN32
    std::wstring cmd;
    for (const auto& arg : args) {
        auto wide = utf16(arg);
        if (wide.empty() && !arg.empty()) return -1;
        if (!cmd.empty()) cmd += L' ';
        cmd += win_quote(wide);
    }
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end()); mutable_cmd.push_back(0);
    std::wstring cwd = opt.working_directory.empty() ? L"" : opt.working_directory.wstring();
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const bool capture = opt.quiet || static_cast<bool>(opt.output);
    HANDLE hread = INVALID_HANDLE_VALUE, hwrite = INVALID_HANDLE_VALUE;
    if (capture) {
        SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
        if (!CreatePipe(&hread, &hwrite, &sa, 0)) return -1;
        SetHandleInformation(hread, HANDLE_FLAG_INHERIT, 0);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hwrite; si.hStdError = hwrite;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    // A Job object ensures stopping CMake also stops the Ninja/GCC children.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) { if (capture) {CloseHandle(hread); CloseHandle(hwrite);} return -1; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof(limit));
    std::vector<wchar_t> environment;
    if(!opt.extra_path.empty()) {
        LPWCH block=GetEnvironmentStringsW();
        if(!block) {if(capture){CloseHandle(hread);CloseHandle(hwrite);}CloseHandle(job);return -1;}
        std::wstring old_path;
        DWORD n=GetEnvironmentVariableW(L"PATH",nullptr,0);
        if(n>0){old_path.resize(n);DWORD got=GetEnvironmentVariableW(L"PATH",old_path.data(),n);old_path.resize(got);}
        for(const wchar_t* p=block;*p;p+=wcslen(p)+1) {
            const std::wstring line(p);
            if(line.size()>=5 && _wcsnicmp(line.c_str(),L"PATH=",5)==0)continue;
            environment.insert(environment.end(),line.begin(),line.end());environment.push_back(0);
        }
        FreeEnvironmentStringsW(block);
        std::wstring path=L"PATH=";
        for(const auto& extra:opt.extra_path){path+=extra.wstring();path+=L';';}
        path+=old_path;
        environment.insert(environment.end(),path.begin(),path.end());
        environment.push_back(0);environment.push_back(0);
    }
    const DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | (environment.empty()?0:CREATE_UNICODE_ENVIRONMENT);
    BOOL created = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr,
                                  capture ? TRUE : FALSE, flags, environment.empty()?nullptr:environment.data(),
                                  cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (capture) CloseHandle(hwrite);
    if (!created) { if (capture) CloseHandle(hread); CloseHandle(job); return -1; }
    // Never resume a process that cannot be supervised as part of its process tree.
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        if (capture) CloseHandle(hread);
        CloseHandle(job); return -1;
    }
    ResumeThread(pi.hThread);
    std::string pending;
    bool cancelled = false;
    for (;;) {
        if (opt.cancel && opt.cancel->requested.load()) {
            cancelled = true; TerminateJobObject(job, 130); break;
        }
        if (capture) {
            DWORD available = 0;
            if (PeekNamedPipe(hread, nullptr, 0, nullptr, &available, nullptr) && available) {
                char buffer[4096]; DWORD got = 0;
                if (ReadFile(hread, buffer, static_cast<DWORD>(std::min<size_t>(sizeof(buffer),available)), &got, nullptr) && got)
                    relay(buffer, got, opt, pending);
            }
        }
        if (WaitForSingleObject(pi.hProcess, 35) == WAIT_OBJECT_0) break;
    }
    // Drain output produced just before exit.
    if (capture) {
        for (;;) {
            DWORD available = 0; char buffer[4096]; DWORD got = 0;
            if (!PeekNamedPipe(hread, nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
            if (!ReadFile(hread, buffer, static_cast<DWORD>(std::min<size_t>(sizeof(buffer),available)), &got, nullptr) || !got) break;
            relay(buffer, got, opt, pending);
        }
        CloseHandle(hread);
    }
    if (!pending.empty() && opt.output) opt.output(pending);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(job);
    return cancelled ? 130 : static_cast<int>(code);
#else
    const bool capture = static_cast<bool>(opt.output);
    int fds[2] = {-1,-1};
    if (capture && pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { if (capture) {close(fds[0]); close(fds[1]);} return -1; }
    if (pid == 0) {
        setpgid(0,0); // child and its descendants have a separate process group
        if(!opt.extra_path.empty()) {
            std::string path;
            for(const auto& extra:opt.extra_path){path+=extra.string();path+=':';}
            const char* current=std::getenv("PATH");if(current)path+=current;
            setenv("PATH",path.c_str(),1);
        }
        if (!opt.working_directory.empty() && chdir(opt.working_directory.c_str()) != 0) _exit(127);
        if (capture) {
            close(fds[0]); dup2(fds[1], STDOUT_FILENO); dup2(fds[1], STDERR_FILENO); close(fds[1]);
        } else if (opt.quiet) {
            std::freopen("/dev/null", "w", stdout); std::freopen("/dev/null", "w", stderr);
        }
        std::vector<char*> argv;
        for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr); execvp(argv[0],argv.data()); _exit(127);
    }
    setpgid(pid,pid); // best effort if child execs before parent schedules
    if (capture) close(fds[1]);
    int status = 0; bool finished = false, cancelled = false, reaped = false;
    std::string pending;
    auto started_cancel = std::chrono::steady_clock::time_point{};
    while (!finished || (capture && !reaped)) {
        if (opt.cancel && opt.cancel->requested.load() && !cancelled) {
            cancelled = true;
            started_cancel = std::chrono::steady_clock::now();
#if defined(__linux__)
            // Freeze the group leader while enumerating descendants so it cannot
            // spawn a new detached process between /proc traversal and SIGTERM.
            kill(pid,SIGSTOP);
            usleep(15000);
            signal_descendants(pid,SIGTERM);
            kill(pid,SIGCONT);
#endif
            kill(-pid,SIGTERM);
        }
        if (cancelled && !finished && std::chrono::steady_clock::now()-started_cancel > std::chrono::milliseconds(500))
        {
#if defined(__linux__)
            signal_descendants(pid,SIGKILL);
#endif
            kill(-pid,SIGKILL);
        }
        if (capture && !reaped) {
            struct pollfd pfd{fds[0], POLLIN | POLLHUP, 0};
            int ready = poll(&pfd,1,50);
            if (ready > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
                char buf[4096]; ssize_t n = read(fds[0],buf,sizeof(buf));
                if (n > 0) relay(buf,static_cast<size_t>(n),opt,pending);
                else if (n <= 0) { close(fds[0]); reaped = true; }
            } else if (ready < 0 && errno != EINTR) { close(fds[0]); reaped = true; }
        } else usleep(50000);
        if (!finished) {
            const pid_t w = waitpid(pid,&status,WNOHANG);
            if (w == pid) finished = true;
            else if (w < 0 && errno != EINTR) finished = true;
        }
        if (finished && !capture) break;
    }
    if (!pending.empty() && opt.output) opt.output(pending);
    if (cancelled) return 130;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128+WTERMSIG(status);
    return -1;
#endif
}
inline int run(const std::vector<std::string>& args, bool quiet = false,
               const std::function<void(const std::string&)>& callback = {}) {
    options o; o.quiet = quiet; o.output = callback; return run_ex(args,o);
}
inline bool available(const std::string& exe) { return !exe.empty() && run({exe,"--version"},true)==0; }
}
#endif
