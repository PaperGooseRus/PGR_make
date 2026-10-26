#ifndef PGR_SAFE_PATHS_HPP
#define PGR_SAFE_PATHS_HPP
#include <filesystem>
#include <string>
#include <system_error>

namespace pgr_paths {
namespace fs = std::filesystem;
inline bool within(const fs::path& child, const fs::path& base) {
    auto ci = child.begin();
    for (auto bi = base.begin(); bi != base.end(); ++bi, ++ci)
        if (ci == child.end() || *bi != *ci) return false;
    return true;
}
inline bool safe_build_dir(const fs::path& target, const fs::path& source_root = ".") {
    if (target.empty()) return false;
    std::error_code ec;
    auto root = fs::weakly_canonical(source_root, ec);
    if (ec) return false;
    auto dest = fs::weakly_canonical(fs::absolute(target, ec), ec);
    if (ec || dest == root || within(root, dest) || !within(dest, root)) return false;
    if (dest == root.root_path()) return false;
    ec.clear();
    if (fs::exists(target, ec) && fs::is_symlink(target, ec)) return false;
    if (ec) return false;
    return true;
}
inline bool clean(const fs::path& target, std::string& error) {
    if (!safe_build_dir(target)) { error = "unsafe build directory: " + target.string(); return false; }
    const auto name = target.lexically_normal().filename().string();
    if (!(name == "build" || name == "build_cmake" || name == "out" ||
          name == "cmake-build-debug" || name == "cmake-build-release" ||
          name.rfind("build-", 0) == 0 || name.rfind("build_", 0) == 0 ||
          name.rfind("build ", 0) == 0)) {
        error = "cleanup requires a dedicated build*/out directory";
        return false;
    }
    std::error_code ec;
    fs::remove_all(target, ec);
    if (ec) { error = ec.message(); return false; }
    return true;
}
}
#endif
