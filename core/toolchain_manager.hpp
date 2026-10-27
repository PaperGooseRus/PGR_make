#ifndef PGR_TOOLCHAIN_MANAGER_HPP
#define PGR_TOOLCHAIN_MANAGER_HPP
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include "pack_tools.hpp"

namespace pgr_tools {
namespace fs = std::filesystem;
struct tool {
    std::string name;
    fs::path path;
    std::string version;
    bool from_pack = false;
};
inline bool executable_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path,ec) && !fs::is_symlink(path,ec);
}
inline fs::path find(const fs::path& pack, const std::string& name,
                     const fs::path& selected_compiler = {}, bool fallback_system = true) {
    if (name.empty()) return {};
    fs::path raw = fs::u8path(name);
    if (raw.is_absolute() || raw.has_parent_path()) return executable_file(raw) ? raw : fs::path{};
    std::vector<fs::path> folders;
    if (!selected_compiler.empty() && selected_compiler.has_parent_path())
        folders.push_back(selected_compiler.parent_path());
    auto pack_folders = pgr_pack::search_directories(pack);
    folders.insert(folders.end(), pack_folders.begin(), pack_folders.end());
    std::vector<std::string> filenames = {name};
#ifdef _WIN32
    if (raw.extension().empty()) filenames.push_back(name + ".exe");
#endif
    for (const auto& folder : folders)
        for (const auto& filename : filenames) {
            fs::path candidate = folder / fs::u8path(filename);
            if (executable_file(candidate)) return candidate;
        }
    // Unlike mutating the process-wide PATH, fallback is an explicit decision.
    return fallback_system ? raw : fs::path{};
}
inline std::vector<tool> inventory(const fs::path& pack) {
    std::vector<tool> result;
    for (const auto& path : pgr_pack::compilers(pack)) {
        result.push_back({path.filename().u8string(), path, {}, true});
    }
    for (auto& name : {"cmake", "ninja", "clangd", "objcopy"}) {
        auto path=find(pack,name,{},false);
        if (!path.empty()) result.push_back({name,path,{},true});
    }
    return result;
}
inline std::string version(const fs::path& executable) {
    if (executable.empty()) return {};
    return pgr_pack::version(executable);
}
}
#endif
