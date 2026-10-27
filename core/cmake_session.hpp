#ifndef PGR_CMAKE_SESSION_HPP
#define PGR_CMAKE_SESSION_HPP
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "workspace.hpp"
#include "safe_paths.hpp"
#include "make.hpp"
#include "tree.hpp"
#include "cmake_layout.hpp"
#include "cmake_writer.hpp"
#include "toolchain.hpp"
#include "presets.hpp"

namespace pgr_cmake {
namespace fs = std::filesystem;

inline bool generate(const pgr_workspace::project_t& project,
                     const make_t::config_t& config, std::string& error) {
    tree_t tree(pgr_workspace::utf8(project.source_root));
    tree_t::options_t opts;
    opts.project_name = pgr_workspace::cmake_target_name(project.name);
    opts.exclude_dirs = {"build", "build_cmake", ".pgr_build", ".git", ".vs", ".vscode", "obj", "bin", "out"};
    opts.include_roots = config.include_dirs;
    opts.only_sources = config.source_files;
    tree.set_options(opts);
    if (tree.scan() != 0) { error = "source scan failed"; return false; }
    if (!tree.cycles().empty()) std::cerr << "PGR: warning: include cycles detected\n";
    cmake_layout_t layout;
    if (layout.build(tree) != 0) { error = "CMake target layout failed"; return false; }
    if (cmake_writer_t::save_staged(tree, layout, config, project.stage()) != 0) {
        error = "cannot generate staged CMakeLists"; return false;
    }
    // Staging regeneration replaces the whole PGR-owned directory.
    // Recreate presets each time so they cannot silently disappear.
    if (!pgr_presets::write(project, config, error)) return false;
    return true;
}

// CMake cannot switch compilers/toolchain in an existing CMakeCache.
// On change, clean *only* a PGR-owned build directory before reconfigure.
inline bool prepare_cache(const pgr_workspace::project_t& project, const fs::path& target,
                          const make_t::config_t& config, std::string& error,
                          bool native = false) {
    if (!pgr_paths::prepare(target, project.source_root, project.store_root, error)) return false;
    const std::string key = "PGR_CMAKE_CACHE_V1\n" + config.compiler + "\n" +
        config.toolchain_file + "\n" + config.toolchain_prefix + "\n" +
        config.package_manager + "\n" + (native ? "native" : "generated") + "\n";
    const fs::path key_file = target / ".pgr_make_cache_key";
    if (fs::exists(target / "CMakeCache.txt")) {
        std::ifstream old(key_file, std::ios::binary);
        const std::string previous((std::istreambuf_iterator<char>(old)), std::istreambuf_iterator<char>());
        if (previous != key) {
            if (!pgr_paths::clean(target, project.source_root, project.store_root, error) ||
                !pgr_paths::prepare(target, project.source_root, project.store_root, error)) return false;
        }
    }
    std::ofstream f(key_file, std::ios::binary | std::ios::trunc);
    f << key;
    if (!f) { error = "cannot save CMake cache identity"; return false; }
    return true;
}
}
#endif
