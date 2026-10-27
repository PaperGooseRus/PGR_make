#ifndef PGR_PACK_TOOLS_HPP
#define PGR_PACK_TOOLS_HPP
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <system_error>
#include "workspace.hpp"
#include "process_runner.hpp"
#include "toolchain.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace pgr_pack {
namespace fs = std::filesystem;

// Tool binaries are not bundled. Discover popular extracted toolchain layouts
// and prepend their bin folders to PATH for the lifetime of PGR_make.
inline std::vector<fs::path> search_directories(const fs::path& pack) {
    std::vector<fs::path> candidates;
    auto insert = [&](const fs::path& dir) {
        std::error_code ec;
        if (fs::is_directory(dir, ec) && !fs::is_symlink(dir, ec))
            candidates.push_back(fs::weakly_canonical(dir, ec));
    };
    insert(pack);
    insert(pack / "bin");
    std::error_code ec;
    for (fs::directory_iterator it(pack, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_directory() || it->is_symlink()) continue;
        auto d = it->path();
        insert(d);
        insert(d / "bin");
        insert(d / "ucrt64" / "bin");
        insert(d / "mingw64" / "bin");
        insert(d / "usr" / "bin");
        // A nested archive: Pack/LLVM/clang-version/bin.
        std::error_code e2;
        for (fs::directory_iterator nested(d, fs::directory_options::skip_permission_denied, e2), last;
             !e2 && nested != last; nested.increment(e2)) {
            if (!nested->is_directory() || nested->is_symlink()) continue;
            insert(nested->path() / "bin");
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}
inline std::vector<fs::path> compilers(const fs::path& pack) {
    std::vector<fs::path> binaries;
    for (const auto& dir : search_directories(pack)) {
        std::error_code ec;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || it->is_symlink()) continue;
            const auto filename = it->path().filename().u8string();
            const bool valid = filename == "gcc" || filename == "g++" ||
                filename == "clang" || filename == "clang++" ||
                filename == "gcc.exe" || filename == "g++.exe" ||
                filename == "clang.exe" || filename == "clang++.exe" ||
                filename == "cl.exe" || filename == "cl" ||
                filename.find("-gcc") != std::string::npos ||
                filename.find("-g++") != std::string::npos;
            if (valid) binaries.push_back(it->path());
        }
    }
    std::sort(binaries.begin(), binaries.end());
    binaries.erase(std::unique(binaries.begin(), binaries.end()), binaries.end());
    return binaries;
}

inline std::string version(const fs::path& executable) {
    std::string result;
    pgr_process::run({pgr_workspace::utf8(executable),
                       pgr_toolchain::classify(pgr_workspace::utf8(executable)) == pgr_toolchain::family::msvc ? "/Bv" : "--version"}, true,
        [&](const std::string& line) { if (result.empty()) result = line; });
    return result;
}

inline bool activate(const fs::path& pack) {
    auto paths = search_directories(pack);
    if (paths.empty()) return false;
#ifdef _WIN32
    DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring old(n ? n : 1, L'\0');
    if (n) { DWORD got = GetEnvironmentVariableW(L"PATH", &old[0], n); old.resize(got); }
    else old.clear();
    std::wstring combined;
    for (const auto& p : paths) { combined += p.wstring(); combined += L';'; }
    combined += old;
    return SetEnvironmentVariableW(L"PATH", combined.c_str()) != 0;
#else
    const char* prior = std::getenv("PATH");
    std::string combined;
    for (const auto& p : paths) { combined += p.string(); combined += ':'; }
    if (prior) combined += prior;
    return setenv("PATH", combined.c_str(), 1) == 0;
#endif
}
}
#endif
