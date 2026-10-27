#ifndef PGR_ARTIFACTS_HPP
#define PGR_ARTIFACTS_HPP
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include "make.hpp"
#include "process_runner.hpp"
#include "workspace.hpp"

namespace pgr_artifacts {
namespace fs=std::filesystem;
// Optional embedded conversion, only next to a user-configured ELF.
// The generated BIN/HEX have their own ownership marker to avoid overwriting
// pre-existing user files and accidental cross-project collisions.
inline bool export_elf(const pgr_workspace::project_t& project,
                       const make_t::config_t& cfg, std::string& error,
                       const std::function<void(const std::string&)>& output = {},
                       pgr_process::cancel_token* cancellation = nullptr) {
    const fs::path configured=fs::u8path(cfg.output_file);
    if (configured.extension() != ".elf" ||
        (cfg.objcopy.empty() && cfg.toolchain_prefix.empty())) return true;
    const fs::path elf=configured.is_absolute() ? configured : project.source_root/configured;
    if (!fs::is_regular_file(elf)) {
        error="ELF output not found: "+pgr_workspace::utf8(elf); return false;
    }
    std::error_code ec;
    const fs::path root=fs::weakly_canonical(project.source_root,ec);
    const fs::path outdir=fs::weakly_canonical(elf.parent_path(),ec);
    if (ec || outdir == root) {
        // Root itself is accepted too, but outside paths are rejected below.
        if (ec) { error="Cannot canonicalize artifact directory"; return false; }
    }
    const auto rel=outdir.lexically_relative(root);
    if (rel.empty() || rel.is_absolute() || (!rel.empty() && *rel.begin()=="..")) {
        if (outdir!=root) { error="Artifact directory outside source root"; return false; }
    }
    const std::string identity="PGR_MAKE_ARTIFACT_V1\n" +
        pgr_workspace::utf8(fs::weakly_canonical(project.store_root)) + "\n" +
        pgr_workspace::utf8(root) + "\n";
    struct destination { fs::path target, marker; std::string format; };
    std::vector<destination> destinations;
    for (const auto& [format,ext] : std::vector<std::pair<std::string,std::string>>{
        {"binary",".bin"},{"ihex",".hex"}}) {
        fs::path dst=elf;dst.replace_extension(ext);
        const fs::path marker=fs::u8path(pgr_workspace::utf8(dst)+".pgr_make_owner");
        ec.clear();
        const bool dst_exists=fs::exists(dst,ec);
        const bool marker_exists=fs::exists(marker,ec);
        if (ec || (dst_exists && fs::is_symlink(dst,ec)) ||
            (marker_exists && fs::is_symlink(marker,ec)) || ec) {
            error="Symlink artifact destination refused"; return false;
        }
        if (dst_exists || marker_exists) {
            std::ifstream f(marker,std::ios::binary);
            std::string previous((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
            if (!fs::is_regular_file(dst,ec) || previous!=identity) {
                error="Refusing to overwrite unowned artifact: "+pgr_workspace::utf8(dst);
                return false;
            }
        }
        destinations.push_back({dst,marker,format});
    }
    const std::string tool=cfg.objcopy.empty() ? cfg.toolchain_prefix+"objcopy" : cfg.objcopy;
    for (const auto& dest:destinations) {
        const std::vector<std::string> args={tool,"-O",dest.format,pgr_workspace::utf8(elf),pgr_workspace::utf8(dest.target)};
        if (output) output("[objcopy] " + pgr_process::display(args));
        pgr_process::options process_options;
        process_options.output=output;
        process_options.cancel=cancellation;
        process_options.working_directory=project.source_root;
        const int rc=pgr_process::run_ex(args,process_options);
        if (rc!=0 || !fs::is_regular_file(dest.target)) {
            error="objcopy conversion failed for "+pgr_workspace::utf8(dest.target);
            return false;
        }
        std::ofstream marker(dest.marker,std::ios::binary|std::ios::trunc);
        marker << identity;
        if (!marker) { error="Cannot save artifact ownership marker"; return false; }
    }
    return true;
}
}
#endif
