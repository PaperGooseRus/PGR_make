#ifndef CMAKE_WRITER_HPP
#define CMAKE_WRITER_HPP

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "cmake_layout.hpp"
#include "make.hpp"
#include "tree.hpp"

namespace fs = std::filesystem;

/**
 * @file cmake_writer.hpp
 * @brief Генерация текста CMakeLists.txt по плану cmake_layout_t.
 */

/**
 * @class cmake_writer_t
 * @brief Превращает (tree_t, cmake_layout_t, глобальные флаги) в файлы.
 *
 * Генерирует:
 *  - корневой CMakeLists.txt: cmake_minimum_required, project(), стандарт,
 *    глобальные флаги, link_directories/link_libraries, add_subdirectory();
 *  - промежуточные CMakeLists.txt: только add_subdirectory();
 *  - «домашние» CMakeLists.txt таргетов: add_library / add_executable,
 *    target_include_directories, target_link_libraries (с PUBLIC/PRIVATE
 *    на основе module_dep_t::is_public).
 *
 * Пути внутри файлов — относительные к каталогу самого CMakeLists.txt.
 * Поля make_t::config_t::source_files / include_dirs / output_file
 * игнорируются: источники и -I вычисляются tree_t per-target.
 */
class cmake_writer_t {
    public:
        static std::map<std::string, std::string> generate(
            const tree_t& tree,
            const cmake_layout_t& layout,
            const make_t::config_t& global)
        {
            std::map<std::string, std::string> out;
            for (const auto& [path, file] : layout.files()) {
                out[path] = render_file(file, layout, tree, global);
            }
            return out;
        }

        static int save(
            const tree_t& tree,
            const cmake_layout_t& layout,
            const make_t::config_t& global)
        {
            const auto files = generate(tree, layout, global);

            for (const auto& [path, content] : files) {
                const fs::path p(path);
                if (p.has_parent_path()) {
                    std::error_code ec;
                    fs::create_directories(p.parent_path(), ec);
                    if (ec) {
                        std::cerr << "cmake_writer_t: cannot create "
                                  << p.parent_path() << ": " << ec.message() << '\n';
                        return 0x01;
                    }
                }
                std::ofstream f(path);
                if (!f) {
                    std::cerr << "cmake_writer_t: cannot open " << path << '\n';
                    return 0x02;
                }
                f << content;
                if (!f) {
                    std::cerr << "cmake_writer_t: write error " << path << '\n';
                    return 0x02;
                }
            }
            return 0x00;
        }

    private:
        static std::string file_dir(const cmake_layout_t::file_t& f) {
            const auto slash = f.path.find_last_of('/');
            return (slash == std::string::npos) ? std::string{} : f.path.substr(0, slash);
        }

        static std::string rel(const std::string& from_dir,
                               const std::string& to_path)
        {
            if (from_dir.empty()) return to_path;
            if (to_path == from_dir) return ".";
            if (to_path.size() > from_dir.size() &&
                to_path.compare(0, from_dir.size() + 1, from_dir + "/") == 0) {
                return to_path.substr(from_dir.size() + 1);
            }

            std::size_t depth = 1;
            for (char c : from_dir) if (c == '/') ++depth;

            std::string prefix;
            for (std::size_t i = 0; i < depth; ++i) prefix += "../";
            return prefix + to_path;
        }

        static std::string render_file(
            const cmake_layout_t::file_t& file,
            const cmake_layout_t& layout,
            const tree_t& tree,
            const make_t::config_t& global)
        {
            std::string s;

            if (file.is_root) {
                s += render_root_header(tree, global);
                s += render_global_flags(global);
                s += render_global_links(global);
            }

            for (const auto& sub : file.subdirs) {
                s += "add_subdirectory(" + sub + ")\n";
            }
            if (!file.subdirs.empty()) s += "\n";

            const std::string dir = file_dir(file);
            for (const auto& tname : file.targets) {
                const target_t* t = layout.target(tname);
                if (!t) continue;
                s += render_target(*t, dir);
            }
            return s;
        }

        static std::string render_root_header(
            const tree_t& tree,
            const make_t::config_t& global)
        {
            std::string s;
            s += "cmake_minimum_required(VERSION 3.16)\n";

            const std::string project = tree.options().project_name.empty()
                ? "app" : tree.options().project_name;
            s += "project(" + project + " CXX)\n\n";

            std::string std_ver = "17";
            if (!global.standard.empty()) {
                const auto pos = global.standard.find("=c++");
                if (pos != std::string::npos) std_ver = global.standard.substr(pos + 4);
            }
            s += "set(CMAKE_CXX_STANDARD " + std_ver + ")\n";
            s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
            s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

            s += "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n\n";
            return s;
        }

        static std::string render_global_flags(const make_t::config_t& global) {
            std::vector<std::string> flags;
            if (!global.optimization.empty()) flags.push_back(global.optimization);
            if (!global.debug.empty())        flags.push_back(global.debug);
            for (const auto& w : global.warnings)   flags.push_back(w);
            for (const auto& s : global.sanitizers) flags.push_back(s);
            if (!global.lto.empty())  flags.push_back(global.lto);
            if (!global.arch.empty()) flags.push_back(global.arch);

            std::string s;

            if (!flags.empty()) {
                s += "add_compile_options(\n";
                for (const auto& f : flags) s += "    " + f + "\n";
                s += ")\n\n";
            }

            if (!global.specific_gcc.empty() || !global.specific_clang.empty()) {
                s += "add_compile_options(\n";
                for (const auto& f : global.specific_gcc) {
                    s += "    $<$<CXX_COMPILER_ID:GNU>:" + f + ">\n";
                }
                for (const auto& f : global.specific_clang) {
                    s += "    $<$<CXX_COMPILER_ID:Clang>:" + f + ">\n";
                }
                s += ")\n\n";
            }
            return s;
        }

        static std::string render_global_links(const make_t::config_t& global) {
            std::string s;
            if (!global.library_dirs.empty()) {
                s += "link_directories(\n";
                for (const auto& d : global.library_dirs) s += "    " + d + "\n";
                s += ")\n\n";
            }
            if (!global.libraries.empty()) {
                s += "link_libraries(";
                for (std::size_t i = 0; i < global.libraries.size(); ++i) {
                    if (i > 0) s += " ";
                    s += global.libraries[i];
                }
                s += ")\n\n";
            }
            return s;
        }

        static std::string render_target(const target_t& t,
                                         const std::string& dir)
        {
            std::string s;

            switch (t.type) {
                case target_t::kind::executable:
                    s += "add_executable(" + t.name;
                    for (const auto& src : t.sources) s += "\n    " + rel(dir, src);
                    s += "\n)\n\n";
                    break;
                case target_t::kind::static_lib:
                    s += "add_library(" + t.name + " STATIC";
                    for (const auto& src : t.sources) s += "\n    " + rel(dir, src);
                    s += "\n)\n\n";
                    break;
                case target_t::kind::shared_lib:
                    s += "add_library(" + t.name + " SHARED";
                    for (const auto& src : t.sources) s += "\n    " + rel(dir, src);
                    s += "\n)\n\n";
                    break;
                case target_t::kind::interface_lib:
                    s += "add_library(" + t.name + " INTERFACE)\n\n";
                    break;
            }

            if (!t.public_include_dirs.empty()) {
                const char* scope =
                    (t.type == target_t::kind::interface_lib) ? "INTERFACE" : "PUBLIC";
                s += "target_include_directories(" + t.name + " " + scope + "\n";
                for (const auto& d : t.public_include_dirs) {
                    s += "    " + rel(dir, d) + "\n";
                }
                s += ")\n\n";
            }

            std::vector<std::string> pub, priv;
            for (const auto& d : t.dependencies) {
                if (d.is_public) pub.push_back(d.module);
                else             priv.push_back(d.module);
            }

            if (t.type == target_t::kind::interface_lib) {
                if (!pub.empty()) {
                    s += "target_link_libraries(" + t.name + " INTERFACE";
                    for (const auto& d : pub) s += " " + d;
                    s += ")\n\n";
                }
            } else {
                if (!pub.empty()) {
                    s += "target_link_libraries(" + t.name + " PUBLIC";
                    for (const auto& d : pub) s += " " + d;
                    s += ")\n\n";
                }
                if (!priv.empty()) {
                    s += "target_link_libraries(" + t.name + " PRIVATE";
                    for (const auto& d : priv) s += " " + d;
                    s += ")\n\n";
                }
            }
            return s;
        }
};

#endif // CMAKE_WRITER_HPP