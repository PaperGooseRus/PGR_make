#ifndef CMAKE_LAYOUT_HPP
#define CMAKE_LAYOUT_HPP

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "tree.hpp"

/**
 * @file cmake_layout.hpp
 * @brief Планирование расположения CMakeLists.txt по дереву проекта.
 */

/**
 * @class cmake_layout_t
 * @brief Определяет, где будут лежать CMakeLists.txt и как они связаны.
 *
 * Порядок add_subdirectory() в каждом файле — топологический по
 * зависимостям таргетов: если таргет из подкаталога A зависит от
 * таргета из подкаталога B, то add_subdirectory(B) идёт раньше
 * add_subdirectory(A). Это исключает ошибку CMake
 * «target not found» при target_link_libraries.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 в графе зависимостей есть циклы (CMake невозможен)
 *  - @c 0x02 нет таргетов
 */
class cmake_layout_t {
    public:
        /// @brief Один CMakeLists.txt в плане.
        struct file_t {
            std::string path;
            std::string parent_path;
            bool is_root = false;
            std::vector<std::string> targets;
            std::vector<std::string> subdirs;
        };

    private:
        std::map<std::string, file_t> files_;
        std::map<std::string, std::string> target_home_;
        std::map<std::string, target_t> target_info_;

        static std::string parent_dir(const std::string& path) {
            const auto slash = path.find_last_of('/');
            return (slash == std::string::npos) ? std::string{} : path.substr(0, slash);
        }

        static std::string leaf_component(const std::string& path) {
            const auto slash = path.find_last_of('/');
            return (slash == std::string::npos) ? path : path.substr(slash + 1);
        }

        file_t& get_or_create(const std::string& path, bool is_root) {
            auto it = files_.find(path);
            if (it != files_.end()) return it->second;

            file_t f;
            f.path = path;
            f.is_root = is_root;
            if (!is_root) f.parent_path = parent_dir(path);

            auto [ins, ok] = files_.emplace(path, std::move(f));
            (void)ok;
            return ins->second;
        }

        /// @brief Все таргеты, чей home-каталог лежит в поддереве @p dir.
        std::set<std::string> targets_in(const std::string& dir) const {
            std::set<std::string> result;
            if (dir.empty()) {
                for (const auto& [name, _] : target_home_) result.insert(name);
                return result;
            }
            const std::string prefix = dir + "/";
            for (const auto& [name, home] : target_home_) {
                if (home.size() > prefix.size() &&
                    home.compare(0, prefix.size(), prefix) == 0) {
                    result.insert(name);
                }
            }
            return result;
        }

        /**
         * @brief Топосорт subdirs одного файла по зависимостям таргетов.
         *
         * Строит граф: ребёнок A → ребёнок B, если таргет из A зависит
         * от таргета из B. DFS-пост-обход даёт порядок B-до-A.
         * При обнаружении цикла оставляет алфавитный порядок.
         */
        void topo_sort_subdirs(file_t& f) {
            if (f.subdirs.size() <= 1) {
                std::sort(f.subdirs.begin(), f.subdirs.end());
                return;
            }

            const std::string fdir = parent_dir(f.path);

            std::map<std::string, std::string> subdir_to_dir;
            for (const auto& s : f.subdirs) {
                subdir_to_dir[s] = fdir.empty() ? s : fdir + "/" + s;
            }

            std::map<std::string, std::set<std::string>> subdir_targets;
            for (const auto& [s, dir] : subdir_to_dir) {
                subdir_targets[s] = targets_in(dir);
            }

            std::map<std::string, std::set<std::string>> deps;
            for (const auto& [s, targets] : subdir_targets) {
                for (const auto& tname : targets) {
                    auto ti = target_info_.find(tname);
                    if (ti == target_info_.end()) continue;
                    for (const auto& d : ti->second.dependencies) {
                        for (const auto& [s2, targets2] : subdir_targets) {
                            if (s2 == s) continue;
                            if (targets2.count(d.module)) {
                                deps[s].insert(s2);
                            }
                        }
                    }
                }
            }

            std::vector<std::string> sorted;
            std::set<std::string> visited, in_stack;
            bool cycle = false;

            std::function<void(const std::string&)> dfs =
                [&](const std::string& node) {
                    if (cycle) return;
                    if (in_stack.count(node)) { cycle = true; return; }
                    if (visited.count(node)) return;
                    in_stack.insert(node);
                    for (const auto& d : deps[node]) dfs(d);
                    in_stack.erase(node);
                    visited.insert(node);
                    sorted.push_back(node);
                };

            for (const auto& s : f.subdirs) dfs(s);

            if (!cycle && sorted.size() == f.subdirs.size()) {
                f.subdirs = sorted;
            } else {
                std::sort(f.subdirs.begin(), f.subdirs.end());
            }
        }

    public:
        cmake_layout_t() = default;
        ~cmake_layout_t() = default;

        int build(const tree_t& tree) {
            files_.clear();
            target_home_.clear();
            target_info_.clear();

            // Include cycles do not imply cycles between link targets.
            // CMake can resolve cyclic static-library dependencies; let it diagnose
            // an actual target-level or linker problem instead of rejecting headers.

            const auto targets = tree.guess_targets();
            if (targets.empty()) {
                std::cerr << "cmake_layout_t: no targets\n";
                return 0x02;
            }

            get_or_create("CMakeLists.txt", true);

            for (const auto& t : targets) {
                target_info_[t.name] = t;

                std::string home;
                if (!t.sources.empty()) {
                    home = parent_dir(t.sources.front());
                } else if (!t.headers.empty()) {
                    home = parent_dir(t.headers.front());
                }

                const std::string file_path = home.empty()
                    ? std::string("CMakeLists.txt")
                    : home + "/CMakeLists.txt";

                auto& f = get_or_create(file_path, home.empty());
                f.targets.push_back(t.name);
                target_home_[t.name] = file_path;

                std::string cur = home;
                while (!cur.empty()) {
                    const std::string parent = parent_dir(cur);
                    const std::string cur_file = cur + "/CMakeLists.txt";
                    const std::string parent_file = parent.empty()
                        ? std::string("CMakeLists.txt")
                        : parent + "/CMakeLists.txt";

                    get_or_create(cur_file, parent.empty());
                    auto& pf = get_or_create(parent_file, parent.empty());

                    const std::string leaf = leaf_component(cur);
                    if (std::find(pf.subdirs.begin(), pf.subdirs.end(), leaf)
                        == pf.subdirs.end()) {
                        pf.subdirs.push_back(leaf);
                    }
                    cur = parent;
                }
            }

            for (auto& [path, f] : files_) {
                topo_sort_subdirs(f);
                std::sort(f.targets.begin(), f.targets.end());
            }

            return 0x00;
        }

        const std::map<std::string, file_t>& files() const { return files_; }
        const std::map<std::string, target_t>& targets() const { return target_info_; }

        const target_t* target(const std::string& name) const {
            auto it = target_info_.find(name);
            return (it != target_info_.end()) ? &it->second : nullptr;
        }

        const file_t* find(const std::string& path) const {
            auto it = files_.find(path);
            return (it != files_.end()) ? &it->second : nullptr;
        }

        std::string path_for_target(const std::string& name) const {
            auto it = target_home_.find(name);
            return (it != target_home_.end()) ? it->second : std::string{};
        }

        void print(std::ostream& os = std::cout) const {
            os << "CMake layout: " << files_.size() << " file(s)\n\n";

            for (const auto& [path, f] : files_) {
                const int depth = static_cast<int>(
                    std::count(path.begin(), path.end(), '/'));
                const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');

                os << indent << (f.is_root ? "[ROOT] " : "") << path << "\n";

                for (const auto& s : f.subdirs) {
                    os << indent << "  + add_subdirectory(" << s << ")\n";
                }
                for (const auto& tname : f.targets) {
                    auto ti = target_info_.find(tname);
                    if (ti == target_info_.end()) {
                        os << indent << "  * target " << tname << "\n";
                        continue;
                    }
                    const auto& t = ti->second;
                    const char* kind =
                        (t.type == target_t::kind::executable)  ? "EXE" :
                        (t.type == target_t::kind::static_lib)  ? "STATIC" :
                        (t.type == target_t::kind::shared_lib)  ? "SHARED" :
                                                                  "INTERFACE";
                    os << indent << "  * target " << tname
                       << " (" << kind << ")\n";
                }
            }
        }

        std::string to_string() const {
            std::ostringstream oss;
            print(oss);
            return oss.str();
        }
};

#endif // CMAKE_LAYOUT_HPP