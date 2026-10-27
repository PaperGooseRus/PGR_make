#ifndef PGR_SAFE_PATHS_HPP
#define PGR_SAFE_PATHS_HPP
#include <filesystem>
#include <fstream>
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

inline bool safe_build_dir(const fs::path& target, const fs::path& source_root) {
    if (target.empty()) return false;
    std::error_code ec;
    auto root = fs::weakly_canonical(source_root, ec);
    if (ec || !fs::is_directory(root)) return false;
    const auto absolute = (target.is_absolute() ? target : root / target).lexically_normal();
    // Examine the *spelled* path too: canonicalizing first hides symlink components.
    if (!within(absolute, root)) return false;
    fs::path walked = root;
    for (const auto& component : absolute.lexically_relative(root)) {
        walked /= component;
        ec.clear();
        if (fs::exists(walked, ec)) {
            if (fs::is_symlink(walked, ec) || ec) return false;
        } else if (ec) return false;
    }
    auto dest = fs::weakly_canonical(absolute, ec);
    if (ec || dest == root || within(root, dest) || !within(dest, root)) return false;
    if (dest == root.root_path()) return false;
    const std::string name = dest.filename().u8string();
    const bool designated = name == "build" || name == "build_cmake" || name == "out" ||
        name == "cmake-build-debug" || name == "cmake-build-release" ||
        name.rfind("build-", 0) == 0 || name.rfind("build_", 0) == 0 ||
        name.rfind("build ", 0) == 0 ||
        dest.parent_path().filename() == ".pgr_build" ||
        (within(dest, root / ".pgr_build") && dest != root / ".pgr_build");
    if (!designated) return false;
    // Never follow symlinks even when their resolved destination is inside the project.
    auto cursor = root;
    auto relative = dest.lexically_relative(root);
    for (auto& component : relative) {
        cursor /= component;
        ec.clear();
        if (fs::exists(cursor, ec)) {
            if (fs::is_symlink(cursor, ec) || ec) return false;
        } else if (ec) return false;
    }
    return true;
}

inline fs::path absolute_build_dir(const fs::path& target, const fs::path& source_root) {
    return target.is_absolute() ? target.lexically_normal() : (source_root / target).lexically_normal();
}

inline fs::path marker_path(const fs::path& dest) { return dest / ".pgr_make_build_owner"; }

inline std::string owner_id(const fs::path& source_root, const fs::path& project_store) {
    std::error_code ec;
    auto src = fs::weakly_canonical(source_root, ec);
    if (ec) return {};
    auto store = fs::weakly_canonical(project_store, ec);
    if (ec) return {};
    return "PGR_MAKE_BUILD_V1\n" + src.generic_u8string() + "\n" + store.generic_u8string() + "\n";
}

inline bool prepare(const fs::path& target, const fs::path& source_root,
                    const fs::path& project_store, std::string& error) {
    if (!safe_build_dir(target, source_root)) { error = "unsafe build directory"; return false; }
    const fs::path dest = absolute_build_dir(target, source_root);
    const auto expected = owner_id(source_root, project_store);
    if (expected.empty()) { error = "invalid project identity"; return false; }
    std::error_code ec;
    if (fs::exists(dest, ec)) {
        if (ec || !fs::is_directory(dest, ec)) { error = "build path is not a directory"; return false; }
        if (fs::exists(marker_path(dest), ec)) {
            if (fs::is_symlink(marker_path(dest), ec)) { error = "symlink build marker"; return false; }
            std::ifstream f(marker_path(dest), std::ios::binary);
            std::string actual((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!f && !f.eof()) { error = "unreadable build marker"; return false; }
            if (actual != expected) { error = "build directory belongs to a different project"; return false; }
            return true;
        }
        if (!fs::is_empty(dest, ec) || ec) {
            error = "refusing unowned non-empty directory: " + dest.u8string();
            return false;
        }
    } else {
        if (!fs::create_directories(dest, ec) && ec) { error = ec.message(); return false; }
    }
    std::ofstream f(marker_path(dest), std::ios::binary | std::ios::trunc);
    f << expected;
    if (!f) { error = "cannot create build ownership marker"; return false; }
    return true;
}

inline bool clean(const fs::path& target, const fs::path& source_root,
                  const fs::path& project_store, std::string& error) {
    if (!safe_build_dir(target, source_root)) { error = "unsafe build directory"; return false; }
    const fs::path dest = absolute_build_dir(target, source_root);
    std::error_code ec;
    if (!fs::exists(dest, ec)) return !ec;
    if (ec || !fs::is_directory(dest, ec) || fs::is_symlink(marker_path(dest), ec)) {
        error = "invalid build directory or marker"; return false;
    }
    std::ifstream f(marker_path(dest), std::ios::binary);
    if (!f) { error = "refusing cleanup without PGR ownership marker"; return false; }
    std::string actual((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Windows refuses to delete an open marker.  Close before remove_all(),
    // including when a previous foreign-owner check failed.
    f.close();
    if (actual != owner_id(source_root, project_store)) {
        error = "refusing cleanup: build belongs to another project"; return false;
    }
    fs::remove_all(dest, ec);
    if (ec) { error = ec.message(); return false; }
    return true;
}
}
#endif
