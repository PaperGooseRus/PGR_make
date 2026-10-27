#ifndef PGR_PRESETS_HPP
#define PGR_PRESETS_HPP
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "diagnostics.hpp"
#include "workspace.hpp"
#include "make.hpp"

namespace pgr_presets {
namespace fs = std::filesystem;
// Write only into the PGR-owned staged tree. Never modify user's CMakePresets.json.
inline bool configure_toolchain(const pgr_workspace::project_t& project,
                                const make_t::config_t& cfg,
                                std::vector<std::string>& args, std::string& error) {
    if (cfg.package_manager == "vcpkg") {
        if (!cfg.toolchain_file.empty()) { error="vcpkg conflicts with toolchain_file"; return false; }
        const char* v=std::getenv("VCPKG_ROOT");
        if (!v || !*v) { error="VCPKG_ROOT not set"; return false; }
        const fs::path file=fs::u8path(v)/"scripts/buildsystems/vcpkg.cmake";
        if (!fs::is_regular_file(file)) { error="vcpkg toolchain missing"; return false; }
        args.push_back("-DCMAKE_TOOLCHAIN_FILE="+file.generic_u8string());
    } else if (cfg.package_manager == "conan" && cfg.toolchain_file.empty()) {
        error="Conan 2: first run conan install; configure toolchain_file with generated conan_toolchain.cmake";
        return false;
    } else if (!cfg.package_manager.empty() && cfg.package_manager != "none" &&
               cfg.package_manager != "conan") {
        error="Unknown package manager: " + cfg.package_manager; return false;
    }
    if (!cfg.package_dirs.empty()) {
        std::string joined;
        for (const auto& item : cfg.package_dirs) {
            fs::path prefix = fs::u8path(item);
            if (prefix.is_relative()) prefix = project.source_root / prefix;
            if (!joined.empty()) joined += ';';
            joined += prefix.lexically_normal().generic_u8string();
        }
        args.push_back("-DCMAKE_PREFIX_PATH=" + joined);
    }
    if (!cfg.toolchain_file.empty()) {
        fs::path f=fs::u8path(cfg.toolchain_file);
        if (f.is_relative()) f=project.source_root/f;
        if (!fs::is_regular_file(f)) { error="Toolchain file missing: "+f.generic_u8string(); return false; }
        args.push_back("-DCMAKE_TOOLCHAIN_FILE="+f.generic_u8string());
    }
    return true;
}
inline bool write(const pgr_workspace::project_t& project,
                  const make_t::config_t& cfg, std::string& error) {
    const auto stage = project.stage();
    if (!fs::is_directory(stage) || !fs::is_regular_file(stage / ".pgr_make_generated_owner")) {
        error="Generate PGR CMake before writing presets"; return false;
    }
    std::string toolchain;
    if (cfg.package_manager == "vcpkg") {
        if (!cfg.toolchain_file.empty()) {
            error="vcpkg and a second toolchain_file require explicit chainloading; select one";
            return false;
        }
        const char* env=std::getenv("VCPKG_ROOT");
        if (!env || !*env || !fs::exists(fs::u8path(env)/"scripts/buildsystems/vcpkg.cmake")) {
            error="Set VCPKG_ROOT to an installed vcpkg directory"; return false;
        }
        toolchain="$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake";
    } else if (!cfg.toolchain_file.empty()) {
        fs::path p=fs::u8path(cfg.toolchain_file);
        if (p.is_relative()) p=project.source_root/p;
        toolchain=p.lexically_normal().generic_u8string();
    } else if (cfg.package_manager == "conan") {
        error="Run conan install first and set toolchain_file to generated conan_toolchain.cmake";
        return false;
    }
    std::ofstream out(stage/"CMakePresets.json", std::ios::binary|std::ios::trunc);
    if (!out) { error="Cannot write CMakePresets.json"; return false; }
    out << "{\n  \"version\": 3,\n  \"configurePresets\": [{\n"
        << "    \"name\": \"pgr-default\",\n"
        << "    \"binaryDir\": \"" << pgr_diagnostics::json_escape(project.build().generic_u8string()) << "\",\n"
        << "    \"cacheVariables\": {\n      \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\"";
    if (!toolchain.empty()) out << ",\n      \"CMAKE_TOOLCHAIN_FILE\": \""
                                << pgr_diagnostics::json_escape(toolchain) << "\"";
    if (!cfg.package_dirs.empty()) {
        std::string prefixes;
        for (const auto& value : cfg.package_dirs) {
            fs::path folder = fs::u8path(value);
            if (folder.is_relative()) folder = project.source_root / folder;
            if (!prefixes.empty()) prefixes += ';';
            prefixes += folder.lexically_normal().generic_u8string();
        }
        out << ",\n      \"CMAKE_PREFIX_PATH\": \""
            << pgr_diagnostics::json_escape(prefixes) << "\"";
    }
    out << "\n    }\n  }],\n  \"buildPresets\": [{\n"
        << "    \"name\": \"pgr-default\", \"configurePreset\": \"pgr-default\"\n"
        << "  }]\n}\n";
    return static_cast<bool>(out);
}
}
#endif
