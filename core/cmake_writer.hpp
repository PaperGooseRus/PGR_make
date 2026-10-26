#ifndef CMAKE_WRITER_HPP
#define CMAKE_WRITER_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cmake_layout.hpp"
#include "make.hpp"
#include "tree.hpp"

// Emits CMake, quoting *all* user-provided paths and flags. The staging API
// never writes into the source tree's existing CMakeLists.txt files.
class cmake_writer_t {
    public:
        static constexpr const char* staged_root = "project/generated_cmake";

        static std::map<std::string, std::string> generate(
            const tree_t& tree, const cmake_layout_t& layout,
            const make_t::config_t& global)
        {
            std::map<std::string, std::string> result;
            for (const auto& item : layout.files())
                result.emplace(item.first, render_file(item.second, layout, tree, global));
            return result;
        }

        // Compatibility entrypoint: now defaults to non-destructive staging.
        static int save(const tree_t& tree, const cmake_layout_t& layout,
                        const make_t::config_t& global) {
            return save_staged(tree, layout, global);
        }

        static int save_staged(const tree_t& tree, const cmake_layout_t& layout,
                               const make_t::config_t& global,
                               const std::filesystem::path& stage = staged_root)
        {
            namespace fs = std::filesystem;
            // Externally supplied configurations are validated before rendering.
            // The public writer also checks below for malformed arbitrary flags.
            for (const auto& group : {global.specific_gcc, global.specific_clang})
                for (const auto& flag : group)
                    if (flag.empty() || flag.find_first_not_of(
                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-=+./:,%") != std::string::npos)
                        return 0x03;
            // Legacy local stage OR registered project metadata directory.
            // Refuse arbitrary paths and symlinked stages. Never touch source CMakeLists.
            std::error_code ec;
            const fs::path normalized = fs::absolute(stage, ec).lexically_normal();
            const fs::path legacy = fs::absolute(staged_root, ec).lexically_normal();
            const bool registered = stage.filename() == "generated_cmake" &&
                fs::is_regular_file(stage.parent_path() / "project.pgr", ec) &&
                !fs::is_symlink(stage.parent_path(), ec);
            if (ec || (normalized != legacy && !registered) ||
                (fs::exists(stage, ec) && fs::is_symlink(stage, ec)) || ec) {
                std::cerr << "cmake stage: unsafe destination\n";
                return 0x03;
            }
            // Stage is destructive: refuse to erase unowned files.
            const fs::path marker = stage / ".pgr_make_generated_owner";
            const std::string signature = "PGR_MAKE_CMAKE_V1\n" +
                fs::weakly_canonical(stage.parent_path(), ec).generic_u8string() + "\n";
            if (ec) return 0x03;
            if (fs::exists(stage, ec)) {
                if (!fs::is_directory(stage, ec) || ec || fs::is_symlink(marker, ec)) return 0x03;
                if (!fs::is_empty(stage, ec)) {
                    std::ifstream current(marker, std::ios::binary);
                    std::string saved((std::istreambuf_iterator<char>(current)), std::istreambuf_iterator<char>());
                    if (saved != signature) {
                        std::cerr << "cmake stage: refusing to overwrite unowned directory\n";
                        return 0x03;
                    }
                }
            }
            fs::remove_all(stage, ec);
            if (ec) return 0x03;
            fs::create_directories(stage, ec);
            if (ec) return 0x03;
            std::ofstream ownership(marker, std::ios::binary);
            ownership << signature;
            if (!ownership) return 0x03;
            // Only metadata is generated here. CMake refers to original source
            // paths, so compile_commands.json and incremental rebuilds remain
            // correct for IDEs, and source files are never duplicated.
            return write_files(generate(tree, layout, global), stage);
        }

    private:
        static bool safe_relative(const std::filesystem::path& path) {
            for (const auto& p : path) if (p == ".." || p == ".") return false;
            return !path.empty();
        }
        static std::string q(const std::string& s) {
            // No untrusted string can end a CMake quoted argument.
            std::string out = "\"";
            for (char c : s) {
                if (c == '\\' || c == '"' || c == '$' || c == ';') out += '\\';
                if (c == '\n' || c == '\r') out += ' ';
                else out += c;
            }
            return out + '"';
        }
        static std::string abs_path(const std::string& p) {
            if (p.empty()) return {};
            std::error_code ec;
            auto result = std::filesystem::absolute(std::filesystem::u8path(p), ec);
            return ec ? p : result.lexically_normal().generic_string();
        }
        static int write_files(const std::map<std::string, std::string>& files,
                               const std::filesystem::path& root) {
            namespace fs = std::filesystem;
            for (const auto& [path, content] : files) {
                fs::path relative(path);
                if (!safe_relative(relative) || relative.is_absolute()) return 0x03;
                fs::path dest = root / relative;
                std::error_code ec;
                fs::create_directories(dest.parent_path(), ec);
                if (ec) return 0x01;
                std::ofstream f(dest, std::ios::binary | std::ios::trunc);
                if (!f || !(f << content)) {
                    std::cerr << "cannot write " << dest << '\n';
                    return 0x02;
                }
            }
            return 0;
        }
        static std::string render_file(const cmake_layout_t::file_t& f,
                                       const cmake_layout_t& layout,
                                       const tree_t& tree,
                                       const make_t::config_t& cfg) {
            std::string s;
            if (f.is_root) {
                s += render_header(tree, layout, cfg);
                s += render_globals(cfg);
            }
            for (const auto& dir : f.subdirs) s += "add_subdirectory(" + q(dir) + ")\n";
            if (!f.subdirs.empty()) s += '\n';
            for (const auto& name : f.targets)
                if (auto t = layout.target(name)) s += render_target(*t, cfg);
            return s;
        }
        static std::string render_header(const tree_t& tree,
                                         const cmake_layout_t& layout,
                                         const make_t::config_t& cfg) {
            bool c = false, cpp = false;
            for (const auto& [_, t] : layout.targets()) for (const auto& src : t.sources) {
                if (std::filesystem::path(src).extension() == ".c") c = true;
                else cpp = true;
            }
            if (!c && !cpp) cpp = true;
            std::string project = tree.options().project_name.empty() ? "app" : tree.options().project_name;
            for (auto& ch : project) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') ch = '_';
            std::string s = "cmake_minimum_required(VERSION 3.16)\n";
            if (!cfg.toolchain_file.empty())
                s += "set(CMAKE_TOOLCHAIN_FILE " + q(abs_path(cfg.toolchain_file)) + ")\n";
            s += "project(" + project + " LANGUAGES" + (c ? " C" : "") + (cpp ? " CXX" : "") + ")\n\n";
            std::string std_ver = "17";
            auto at = cfg.standard.find("=c++");
            if (at != std::string::npos) std_ver = cfg.standard.substr(at + 4);
            if (std_ver.find_first_not_of("0123456789") != std::string::npos) std_ver = "17";
            s += "set(CMAKE_CXX_STANDARD " + std_ver + ")\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
            s += "set(CMAKE_CXX_EXTENSIONS OFF)\n";
            if (!cfg.c_standard.empty()) {
                std::string v = cfg.c_standard;
                auto eq = v.find("=c");
                if (eq != std::string::npos) v = v.substr(eq + 2);
                if (v.find_first_not_of("0123456789") == std::string::npos)
                    s += "set(CMAKE_C_STANDARD " + v + ")\nset(CMAKE_C_STANDARD_REQUIRED ON)\n";
            }
            s += "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n\n";
            return s;
        }
        static std::string render_globals(const make_t::config_t& cfg) {
            std::string s;
            for (const auto& pkg : cfg.packages) {
                auto bar = pkg.find('|');
                std::string name = pkg.substr(0, bar);
                if (!name.empty()) s += "find_package(" + q(name) + " REQUIRED)\n";
            }
            if (!cfg.packages.empty()) s += '\n';
            std::vector<std::string> flags;
            for (const auto& f : {cfg.optimization, cfg.debug, cfg.arch, cfg.lto})
                if (!f.empty()) flags.push_back(f);
            for (const auto& f : cfg.warnings) if (!f.empty()) flags.push_back(f);
            for (const auto& f : cfg.sanitizers) if (!f.empty()) flags.push_back(f);
            if (!flags.empty()) {
                s += "add_compile_options(\n";
                for (const auto& f : flags) s += "  " + q(f) + "\n";
                s += ")\n\n";
            }
            if (!cfg.specific_gcc.empty() || !cfg.specific_clang.empty()) {
                s += "add_compile_options(\n";
                for (const auto& f : cfg.specific_gcc) {
                    s += "  \"$<$<COMPILE_LANG_AND_ID:C,GNU>:" + f + ">\"\n";
                    s += "  \"$<$<COMPILE_LANG_AND_ID:CXX,GNU>:" + f + ">\"\n";
                }
                for (const auto& f : cfg.specific_clang) {
                    s += "  \"$<$<COMPILE_LANG_AND_ID:C,Clang>:" + f + ">\"\n";
                    s += "  \"$<$<COMPILE_LANG_AND_ID:CXX,Clang>:" + f + ">\"\n";
                }
                s += ")\n\n";
            }
            // Sanitizers, LTO and embedded linker scripts need link-time options.
            std::vector<std::string> link_flags = cfg.sanitizers;
            if (!cfg.lto.empty()) link_flags.push_back(cfg.lto);
            if (!cfg.linker_script.empty()) {
                link_flags.push_back("-T");
                link_flags.push_back(abs_path(cfg.linker_script));
            }
            if (!link_flags.empty()) {
                s += "add_link_options(\n";
                for (const auto& f : link_flags) if (!f.empty()) s += "  " + q(f) + "\n";
                s += ")\n\n";
            }
            if (!cfg.include_dirs.empty()) {
                s += "include_directories(\n";
                for (const auto& d : cfg.include_dirs) s += "  " + q(abs_path(d)) + "\n";
                s += ")\n\n";
            }
            if (!cfg.library_dirs.empty()) {
                s += "link_directories(\n";
                for (const auto& d : cfg.library_dirs) s += "  " + q(abs_path(d)) + "\n";
                s += ")\n\n";
            }
            std::vector<std::string> libs;
            for (const auto& l : cfg.libraries) if (!l.empty()) libs.push_back(l);
            for (const auto& pkg : cfg.packages) {
                auto bar = pkg.find('|');
                libs.push_back(bar == std::string::npos ? pkg + "::" + pkg : pkg.substr(bar + 1));
            }
            if (!libs.empty()) {
                s += "link_libraries(\n";
                for (const auto& l : libs) s += "  " + q(l) + "\n";
                s += ")\n\n";
            }
            return s;
        }
        static std::string render_target(const target_t& t,
                                         const make_t::config_t& cfg) {
            std::string s;
            auto n = q(t.name);
            auto kind = t.type;
            if (kind == target_t::kind::static_lib && cfg.target_type == "shared")
                kind = target_t::kind::shared_lib;
            if (kind == target_t::kind::interface_lib) s += "add_library(" + n + " INTERFACE)\n";
            else {
                std::string verb = kind == target_t::kind::executable ? "add_executable(" : "add_library(";
                s += verb + n;
                if (kind == target_t::kind::static_lib) s += " STATIC";
                if (kind == target_t::kind::shared_lib) s += " SHARED";
                for (const auto& source : t.sources) s += "\n  " + q(abs_path(source));
                s += "\n)\n";
            }
            if (kind == target_t::kind::executable && !cfg.output_file.empty()) {
                auto path = std::filesystem::path(cfg.output_file);
                auto stem = path.stem().string();
                auto dirout = path.has_parent_path() ? abs_path(path.parent_path().string()) : "";
                if (!stem.empty()) s += "set_target_properties(" + n + " PROPERTIES OUTPUT_NAME " + q(stem) + ")\n";
                if (!dirout.empty()) s += "set_target_properties(" + n + " PROPERTIES RUNTIME_OUTPUT_DIRECTORY " + q(dirout) + ")\n";
            }
            auto scope = kind == target_t::kind::interface_lib ? " INTERFACE" : " PUBLIC";
            if (!t.public_include_dirs.empty() || !t.extra_includes.empty()) {
                s += "target_include_directories(" + n + scope + "\n";
                for (const auto& d : t.public_include_dirs) s += "  " + q(d.empty() ? abs_path(".") : abs_path(d)) + "\n";
                for (const auto& d : t.extra_includes) s += "  " + q(abs_path(d)) + "\n";
                s += ")\n";
            }
            if (!t.extra_flags.empty()) {
                s += "target_compile_options(" + n + (kind == target_t::kind::interface_lib ? " INTERFACE" : " PRIVATE") + "\n";
                for (const auto& f : t.extra_flags) s += "  " + q(f) + "\n";
                s += ")\n";
            }
            std::vector<std::string> pub, priv;
            for (const auto& d : t.dependencies) (d.is_public ? pub : priv).push_back(d.module);
            for (const auto& d : t.extra_links) priv.push_back(d);
            if (kind == target_t::kind::interface_lib) {
                pub.insert(pub.end(), priv.begin(), priv.end());
                priv.clear();
            }
            if (!pub.empty()) {
                s += "target_link_libraries(" + n + (kind == target_t::kind::interface_lib ? " INTERFACE" : " PUBLIC") + "\n";
                for (const auto& dep : pub) s += "  " + q(dep) + "\n";
                s += ")\n";
            }
            if (!priv.empty()) {
                s += "target_link_libraries(" + n + " PRIVATE\n";
                for (const auto& dep : priv) s += "  " + q(dep) + "\n";
                s += ")\n";
            }
            return s + '\n';
        }
};
#endif
