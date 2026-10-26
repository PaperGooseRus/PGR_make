#ifndef TREE_HPP
#define TREE_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

/**
 * @file tree.hpp
 * @brief Сканер дерева проекта: модули, зависимости, циклы, таргеты.
 */

/**
 * @brief Разрешённый #include "..." внутри проекта.
 */
struct resolved_include_t {
    std::string path;          ///< Путь относительно корня проекта.
    std::string include_root;  ///< -I-корень, через который файл нашёлся ("" = root).
};

/**
 * @brief Зависимости одного исходного файла.
 */
struct source_dep_t {
    std::string file;                         ///< Путь относительно корня.
    std::vector<resolved_include_t> includes; ///< Разрешённые локальные include.
    bool has_main = false;                    ///< Найдено ли определение main().
};

/**
 * @brief Зависимости одного заголовочного файла.
 */
struct header_dep_t {
    std::string file;                         ///< Путь относительно корня.
    std::vector<resolved_include_t> includes; ///< Разрешённые локальные include.
};

/**
 * @brief Межмодульная зависимость с признаком публичности.
 *
 * @c is_public = true, если ребро возникло из #include в заголовке —
 * значит, зависимость должна быть PUBLIC в CMake.
 */
struct module_dep_t {
    std::string module;      ///< Каталог другого модуля относительно корня.
    bool is_public = false;  ///< Публичная зависимость (видна потребителям).
};

/**
 * @brief Модуль — каталог с исходниками и/или заголовками.
 */
struct module_t {
    std::string dir;                              ///< Каталог относительно корня ("" для корня).
    std::vector<std::string> sources;             ///< .cpp файлы непосредственно в каталоге.
    std::vector<std::string> headers;             ///< .hpp/.h файлы непосредственно в каталоге.
    std::vector<std::string> public_headers;      ///< Заголовки, включаемые из других модулей.
    std::vector<std::string> public_include_dirs; ///< -I для публичных заголовков.
    std::vector<module_dep_t> dependencies;       ///< Другие модули, от которых зависит этот.
    bool has_main = false;                        ///< Есть main() в одном из sources.

    bool is_header_only() const { return sources.empty() && !headers.empty(); }
    bool is_empty() const { return sources.empty() && headers.empty(); }
};

/**
 * @brief Целевой артефакт сборки — то, во что превратится модуль (или
 *        группа модулей) в CMake.
 */
struct target_t {
    enum class kind {
        executable,     ///< add_executable
        static_lib,     ///< add_library(... STATIC)
        shared_lib,     ///< add_library(... SHARED)
        interface_lib   ///< add_library(... INTERFACE)
    };

    std::string name;                            ///< Имя таргета ("core", "app").
    kind type = kind::static_lib;                ///< Вид таргета.
    std::vector<std::string> sources;            ///< .cpp (для exe/lib).
    std::vector<std::string> headers;            ///< Заголовки (для IDE/install).
    std::vector<std::string> public_include_dirs;///< -I, публикуемые потребителям.
    std::vector<module_dep_t> dependencies;      ///< Другие таргеты (по именам).
    std::vector<std::string> merged_modules;     ///< Каталоги, вошедшие в этот таргет.
};

/**
 * @class tree_t
 * @brief Обходит проект и строит модель «модули + зависимости + таргеты».
 *
 * Поддерживаются три раскладки проекта:
 *  1. Flat:    main.cpp, util.hpp в корне.
 *  2. Split:   include/core/util.hpp + src/core/util.cpp.
 *  3. Modular: modules/module1/{inc,src}/... + modules/module2/...
 *
 * Слияние модулей в таргеты — через удаление компонентов-«контейнеров»
 * (`include`, `src`, `inc`, `lib`, ...) на любом уровне пути, с
 * последующим взятием последнего компонента как имени таргета.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 корень не существует или не каталог
 *  - @c 0x02 ошибка обхода файловой системы
 */
class tree_t {
    public:
        /// @brief Настройки сканера.
        struct options_t {
            /// @brief Каталоги, которые не обходятся.
            std::vector<std::string> exclude_dirs = {
                "build", ".git", ".vs", ".vscode", "obj", "bin", "out"
            };

            std::vector<std::string> source_ext = {".cpp", ".cc", ".cxx", ".c"};
            std::vector<std::string> header_ext = {".hpp", ".h", ".hxx", ".hh"};

            /// @brief Корни для -I. Пусто — определяются автоматически.
            std::vector<std::string> include_roots;

            /**
             * @brief Имена компонентов-«контейнеров», снимаемых при
             *        вычислении логического ключа модуля.
             *
             * Работает на любом уровне: `modules/module1/inc/` →
             * `modules/module1`; `src/core` → `core`.
             */
            std::vector<std::string> container_names = {
                "include", "inc", "public",
                "src", "source", "sources", "lib"
            };

            /**
             * @brief Имя таргета для корня проекта (файлы без подкаталога).
             *
             * Обычно совпадает с project(...) в CMake.
             */
            std::string project_name = "app";

            bool resolve_relative_to_file = true;
            bool resolve_relative_to_root = true;
        };

    private:
        std::string root_;
        options_t opts_;
        std::vector<source_dep_t> sources_;
        std::vector<header_dep_t> headers_;
        std::map<std::string, module_t> modules_;

        // ---------- Утилиты ----------

        static std::string normalize(const fs::path& p) {
            return p.generic_string();
        }

        static std::string parent_dir(const std::string& path) {
            auto slash = path.find_last_of('/');
            return (slash == std::string::npos) ? std::string{} : path.substr(0, slash);
        }

        /// @brief Последний компонент пути ("a/b/c" → "c", "" → "").
        static std::string last_component(const std::string& path) {
            const auto pos = path.find_last_of('/');
            return (pos == std::string::npos) ? path : path.substr(pos + 1);
        }

        /// @brief Заменить '/' на '_' (для CMake-safe имён таргетов).
        static std::string safe_name(const std::string& s) {
            std::string r = s;
            for (auto& c : r) if (c == '/') c = '_';
            return r;
        }

        static std::string read_include_path(const std::string& line) {
            auto i = line.find_first_not_of(" \t");
            if (i == std::string::npos || line[i] != '#') return {};
            ++i;
            i = line.find_first_not_of(" \t", i);
            if (i == std::string::npos) return {};
            if (line.compare(i, 7, "include") != 0) return {};
            i += 7;
            i = line.find_first_not_of(" \t", i);
            if (i == std::string::npos || line[i] != '"') return {};
            auto q2 = line.find('"', i + 1);
            if (q2 == std::string::npos) return {};
            return line.substr(i + 1, q2 - i - 1);
        }

        /**
         * @brief Проверить строку на определение main().
         *
         * Ловит `int main(`, `int main (`, `auto main() -> int`.
         * Ложные срабатывания (`my_main(`) отсекаются проверкой
         * предыдущего символа на «идентификаторность».
         */
        static bool is_main_definition(const std::string& line) {
            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos) return false;
            if (line[start] == '/' &&
                start + 1 < line.size() && line[start + 1] == '/') return false;

            auto pos = line.find("main", start);
            if (pos == std::string::npos) return false;

            if (pos > 0) {
                char c = line[pos - 1];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                    return false;
                }
            }

            auto next = pos + 4;
            while (next < line.size() &&
                   (line[next] == ' ' || line[next] == '\t')) ++next;
            if (next >= line.size() || line[next] != '(') return false;

            return true;
        }

        bool is_excluded(const std::string& rel_dir) const {
            for (const auto& ex : opts_.exclude_dirs) {
                if (rel_dir == ex) return true;
                if (rel_dir.size() > ex.size() &&
                    rel_dir.compare(0, ex.size() + 1, ex + "/") == 0) {
                    return true;
                }
            }
            return false;
        }

        bool has_ext(const fs::path& p,
                     const std::vector<std::string>& exts) const
        {
            const auto e = p.extension().string();
            for (const auto& x : exts) if (e == x) return true;
            return false;
        }

        // ---------- Фаза 1: обход ----------

        int scan_files() {
            std::error_code ec;
            if (!fs::is_directory(root_, ec)) {
                std::cerr << "tree_t: not a directory: " << root_ << '\n';
                return 0x01;
            }

            for (auto it = fs::recursive_directory_iterator(root_, ec);
                 it != fs::recursive_directory_iterator(); ++it)
            {
                if (ec) {
                    std::cerr << "tree_t: scan error: " << ec.message() << '\n';
                    return 0x02;
                }

                const auto& p = it->path();
                const auto rel = fs::relative(p, root_, ec);
                if (ec) continue;
                const std::string rel_norm = normalize(rel);

                if (it->is_directory()) {
                    if (is_excluded(rel_norm)) it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file()) continue;

                if (has_ext(p, opts_.source_ext)) {
                    sources_.push_back({rel_norm, {}, false});
                } else if (has_ext(p, opts_.header_ext)) {
                    headers_.push_back({rel_norm, {}});
                }
            }
            return 0x00;
        }

        // ---------- Фаза 2: include-roots ----------

        /**
         * @brief Определить -I-корни для разрешения #include.
         *
         * Включает:
         *  - "" (корень проекта — для модульной раскладки);
         *  - каталог каждого заголовка;
         *  - родителя каталога заголовка (для split-раскладки:
         *    `include/core/util.hpp` → `-Iinclude`).
         */
        void detect_include_roots() {
            std::set<std::string> roots;

            roots.insert("");  // корень проекта

            for (const auto& h : headers_) {
                const std::string p1 = parent_dir(h.file);  // include/core
                const std::string p2 = parent_dir(p1);      // include

                roots.insert(p1);
                roots.insert(p2);
            }

            opts_.include_roots.assign(roots.begin(), roots.end());
        }

        // ---------- Фаза 3: разрешение include ----------

        /**
         * @brief Найти файл для @p inc относительно @p src_rel.
         *
         * Порядок: include_roots → относительно исходника → относительно
         * корня. include_roots первыми, чтобы публичные -I были
         * предсказуемыми.
         */
        resolved_include_t resolve_include(const std::string& inc,
                                           const std::string& src_rel) const
        {
            std::vector<std::pair<fs::path, std::string>> candidates;

            for (const auto& r : opts_.include_roots) {
                candidates.emplace_back(fs::path(root_) / r / inc, r);
            }
            if (opts_.resolve_relative_to_file) {
                const fs::path dir = fs::path(src_rel).parent_path();
                candidates.emplace_back(fs::path(root_) / dir / inc, normalize(dir));
            }
            if (opts_.resolve_relative_to_root) {
                candidates.emplace_back(fs::path(root_) / inc, "");
            }

            for (const auto& [path, include_root] : candidates) {
                std::error_code ec;
                if (fs::is_regular_file(path, ec)) {
                    const auto rel = fs::relative(path, root_, ec);
                    if (ec) continue;
                    return {normalize(rel), include_root};
                }
            }
            return {};
        }

        int parse_includes_of(const std::string& rel,
                              std::vector<resolved_include_t>& out,
                              bool& has_main)
        {
            std::ifstream in(fs::path(root_) / rel);
            if (!in) return 0x00;

            std::set<std::string> seen;
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();

                const std::string inc = read_include_path(line);
                if (!inc.empty() && !seen.count(inc)) {
                    seen.insert(inc);
                    auto resolved = resolve_include(inc, rel);
                    if (!resolved.path.empty()) out.push_back(resolved);
                }

                if (!has_main && is_main_definition(line)) has_main = true;
            }
            return 0x00;
        }

        int parse_includes() {
            for (auto& s : sources_) {
                parse_includes_of(s.file, s.includes, s.has_main);
            }
            for (auto& h : headers_) {
                bool dummy = false;
                parse_includes_of(h.file, h.includes, dummy);
            }
            return 0x00;
        }

        // ---------- Фаза 4: модули и зависимости ----------

        int build_modules() {
            auto ensure = [this](const std::string& dir) -> module_t& {
                auto& m = modules_[dir];
                m.dir = dir;
                return m;
            };

            for (const auto& s : sources_) {
                auto& m = ensure(parent_dir(s.file));
                m.sources.push_back(s.file);
                if (s.has_main) m.has_main = true;
            }
            for (const auto& h : headers_) {
                ensure(parent_dir(h.file)).headers.push_back(h.file);
            }

            // Рёбра: from_mod → to_mod → is_public (OR по всем рёбрам).
            std::map<std::string, std::map<std::string, bool>> deps;
            std::map<std::string, std::set<std::string>> pub_headers;
            std::map<std::string, std::set<std::string>> pub_include_dirs;

            auto add_edge = [&](const std::string& from_mod,
                                const std::string& inc_path,
                                const std::string& inc_root,
                                bool is_public)
            {
                const std::string to_mod = parent_dir(inc_path);
                if (to_mod == from_mod) return;
                auto& slot = deps[from_mod][to_mod];
                slot = slot || is_public;
                pub_headers[to_mod].insert(inc_path);
                pub_include_dirs[to_mod].insert(inc_root);
            };

            // Из .cpp — рёбра по-private.
            for (const auto& s : sources_) {
                const std::string from = parent_dir(s.file);
                for (const auto& inc : s.includes) {
                    add_edge(from, inc.path, inc.include_root, /*public=*/false);
                }
            }

            // Из .hpp — рёбра по-public (транзитивность).
            for (const auto& h : headers_) {
                const std::string from = parent_dir(h.file);
                for (const auto& inc : h.includes) {
                    add_edge(from, inc.path, inc.include_root, /*public=*/true);
                }
            }

            for (auto& [name, m] : modules_) {
                if (pub_headers.count(name)) {
                    m.public_headers.assign(pub_headers[name].begin(),
                                            pub_headers[name].end());
                }
                if (pub_include_dirs.count(name)) {
                    m.public_include_dirs.assign(pub_include_dirs[name].begin(),
                                                 pub_include_dirs[name].end());
                }
                if (deps.count(name)) {
                    for (const auto& [to, is_pub] : deps[name]) {
                        m.dependencies.push_back({to, is_pub});
                    }
                }
            }
            return 0x00;
        }

    public:
        tree_t() = default;

        explicit tree_t(std::string root) : root_(std::move(root)) {}

        void set_root(std::string r) { root_ = std::move(r); }
        void set_options(const options_t& o) { opts_ = o; }
        const options_t& options() const { return opts_; }

        /// @brief Полный цикл: обход → include-roots → парсинг → модули.
        int scan() {
            sources_.clear();
            headers_.clear();
            modules_.clear();

            int st = scan_files();
            if (st != 0x00) return st;

            if (opts_.include_roots.empty()) detect_include_roots();

            st = parse_includes();
            if (st != 0x00) return st;

            return build_modules();
        }

        const std::string& root() const { return root_; }
        const std::vector<source_dep_t>& sources() const { return sources_; }
        const std::vector<header_dep_t>& headers() const { return headers_; }
        const std::map<std::string, module_t>& modules() const { return modules_; }

        /// @brief Сводка (для отладки).
        void print_summary(std::ostream& os = std::cout) const {
            os << "Root:    " << root_ << '\n'
               << "Sources: " << sources_.size() << '\n'
               << "Headers: " << headers_.size() << '\n'
               << "Modules: " << modules_.size() << '\n';

            for (const auto& [name, m] : modules_) {
                os << "\n[" << (name.empty() ? "." : name) << "]"
                   << (m.has_main ? "  [main]" : "") << '\n';
                for (const auto& s : m.sources) os << "  src: " << s << '\n';
                if (!m.public_headers.empty()) {
                    os << "  public:\n";
                    for (const auto& h : m.public_headers) os << "    " << h << '\n';
                }
                if (!m.public_include_dirs.empty()) {
                    os << "  public -I:";
                    for (const auto& d : m.public_include_dirs)
                        os << " " << (d.empty() ? "." : d);
                    os << '\n';
                }
                if (!m.dependencies.empty()) {
                    os << "  deps:";
                    for (const auto& d : m.dependencies) {
                        os << " " << (d.module.empty() ? "." : d.module);
                        if (d.is_public) os << "*";
                    }
                    os << "   (* = public)";
                    os << '\n';
                }
            }
        }

        // ==================================================================
        // Graphviz
        // ==================================================================

        /**
         * @brief Сериализовать граф модулей в формат Graphviz DOT.
         */
        std::string to_dot() const {
            std::string s;
            s += "digraph project {\n";
            s += "    rankdir=LR;\n";
            s += "    node [shape=box, style=rounded, fontname=\"Helvetica\"];\n";
            s += "    edge [fontname=\"Helvetica\", fontsize=9];\n";

            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;

                std::string label = (name.empty() ? "." : name);
                label += "\\n(" + std::to_string(m.sources.size()) + " src, "
                       + std::to_string(m.headers.size()) + " hdr)";

                std::string attrs;
                if (m.is_header_only()) {
                    attrs = " [style=\"rounded,dashed\"]";
                } else if (m.has_main) {
                    attrs = " [shape=doubleoctagon]";
                }
                s += "    \"" + (name.empty() ? "." : name)
                   + "\" [label=\"" + label + "\"]" + attrs + ";\n";
            }

            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;
                for (const auto& d : m.dependencies) {
                    const std::string style = d.is_public ? "solid" : "dashed";
                    s += "    \"" + (name.empty() ? "." : name) + "\" -> \""
                       + (d.module.empty() ? "." : d.module) + "\""
                       + " [style=\"" + style + "\"];\n";
                }
            }

            s += "}\n";
            return s;
        }

        /// @brief Записать граф в файл.
        int write_dot(const std::string& path) const {
            std::ofstream out(path);
            if (!out) return 0x01;
            out << to_dot();
            return out ? 0x00 : 0x02;
        }

        // ==================================================================
        // Поиск циклов (Тарьян, итеративный)
        // ==================================================================

        /**
         * @brief Найти все циклы в графе модулей.
         *
         * CMake не поддерживает циклические зависимости между таргетами,
         * поэтому такой граф нельзя напрямую превращать в CMakeLists.
         *
         * @return Список циклов; пустой = граф ациклический.
         */
        std::vector<std::vector<std::string>> cycles() const {
            std::map<std::string, std::vector<std::string>> graph;
            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;
                graph[name];
                for (const auto& d : m.dependencies) {
                    graph[name].push_back(d.module);
                }
            }

            std::map<std::string, int> index, lowlink;
            std::set<std::string> on_stack;
            std::vector<std::string> stack;
            int next_index = 0;
            std::vector<std::vector<std::string>> sccs;

            struct frame_t {
                std::string node;
                std::size_t child = 0;
                bool entered = false;
            };

            for (const auto& [start, _] : graph) {
                if (index.count(start)) continue;

                std::vector<frame_t> call_stack;
                call_stack.push_back({start, 0, false});

                while (!call_stack.empty()) {
                    auto& fr = call_stack.back();

                    if (!fr.entered) {
                        index[fr.node] = lowlink[fr.node] = next_index++;
                        stack.push_back(fr.node);
                        on_stack.insert(fr.node);
                        fr.entered = true;
                    }

                    if (fr.child < graph[fr.node].size()) {
                        const std::string& w = graph[fr.node][fr.child++];
                        if (!index.count(w)) {
                            call_stack.push_back({w, 0, false});
                        } else if (on_stack.count(w)) {
                            lowlink[fr.node] = std::min(lowlink[fr.node], index[w]);
                        }
                    } else {
                        if (lowlink[fr.node] == index[fr.node]) {
                            std::vector<std::string> scc;
                            while (true) {
                                std::string w = stack.back(); stack.pop_back();
                                on_stack.erase(w);
                                scc.push_back(w);
                                if (w == fr.node) break;
                            }
                            sccs.push_back(std::move(scc));
                        }
                        const std::string done = fr.node;
                        call_stack.pop_back();
                        if (!call_stack.empty()) {
                            auto& parent = call_stack.back();
                            lowlink[parent.node] = std::min(
                                lowlink[parent.node], lowlink[done]);
                        }
                    }
                }
            }

            std::vector<std::vector<std::string>> result;
            for (auto& scc : sccs) {
                if (scc.size() > 1) {
                    result.push_back(scc);
                } else if (scc.size() == 1) {
                    auto it = graph.find(scc[0]);
                    if (it != graph.end()) {
                        const auto& edges = it->second;
                        if (std::find(edges.begin(), edges.end(), scc[0])
                            != edges.end()) {
                            result.push_back(scc); // self-loop
                        }
                    }
                }
            }
            return result;
        }

        // ==================================================================
        // Эвристика таргетов
        // ==================================================================

    private:
        /**
         * @brief Логический ключ модуля — путь без компонентов-контейнеров.
         *
         * `src/core` → `core`; `include/core` → `core`;
         * `modules/module1/inc` → `modules/module1`;
         * `src` → `` (корень).
         */
        std::string logical_name(const std::string& dir) const {
            if (dir.empty()) return {};

            std::vector<std::string> parts;
            std::string cur;
            for (char c : dir) {
                if (c == '/') {
                    if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()) parts.push_back(cur);

            std::vector<std::string> kept;
            for (const auto& p : parts) {
                bool is_container = false;
                for (const auto& cn : opts_.container_names) {
                    if (p == cn) { is_container = true; break; }
                }
                if (!is_container) kept.push_back(p);
            }

            std::string result;
            for (std::size_t i = 0; i < kept.size(); ++i) {
                if (i > 0) result += "/";
                result += kept[i];
            }
            return result;
        }

    public:
        /**
         * @brief Слить модули в логические таргеты (эвристика).
         *
         * Ключ группировки — полный логический путь (после снятия
         * контейнеров). Имя таргета:
         *  - для корня — @c opts_.project_name;
         *  - для остальных — последний компонент логического ключа;
         *  - при коллизии коротких имён — полный ключ с '/' → '_'.
         *
         * Тип таргета:
         *  - нет sources → INTERFACE library;
         *  - есть sources + main() → EXECUTABLE;
         *  - есть sources без main() → STATIC library.
         *
         * @return Список таргетов, отсортированный по имени.
         */
        std::vector<target_t> guess_targets() const {
            // 1. Группировка по полному логическому ключу.
            std::map<std::string, std::vector<std::string>> groups;
            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;
                groups[logical_name(name)].push_back(name);
            }

            // 2. Подсчёт коротких имён для collision detection.
            std::map<std::string, int> short_count;
            for (const auto& [logical, _] : groups) {
                short_count[last_component(logical)]++;
            }

            // 3. Собрать таргеты.
            std::vector<target_t> targets;
            std::map<std::string, std::string> module_to_target;

            for (auto& [logical, dirs] : groups) {
                target_t t;

                const std::string short_name = last_component(logical);
                if (logical.empty()) {
                    t.name = opts_.project_name;
                } else if (short_count[short_name] == 1) {
                    t.name = short_name;
                } else {
                    t.name = safe_name(logical);
                }

                bool has_sources = false;
                bool has_main = false;

                for (const auto& dir : dirs) {
                    const auto& m = modules_.at(dir);
                    t.merged_modules.push_back(dir);
                    module_to_target[dir] = t.name;

                    for (const auto& s : m.sources) t.sources.push_back(s);
                    for (const auto& h : m.headers) t.headers.push_back(h);
                    for (const auto& d : m.public_include_dirs)
                        t.public_include_dirs.push_back(d);

                    has_sources = has_sources || !m.sources.empty();
                    has_main    = has_main    || m.has_main;
                }

                if (!has_sources)      t.type = target_t::kind::interface_lib;
                else if (has_main)     t.type = target_t::kind::executable;
                else                   t.type = target_t::kind::static_lib;

                targets.push_back(std::move(t));
            }

            // 4. Агрегировать зависимости на уровне таргетов.
            for (auto& t : targets) {
                std::map<std::string, bool> agg;
                for (const auto& dir : t.merged_modules) {
                    const auto& m = modules_.at(dir);
                    for (const auto& dep : m.dependencies) {
                        auto it = module_to_target.find(dep.module);
                        if (it == module_to_target.end()) continue;
                        if (it->second == t.name) continue;
                        auto& slot = agg[it->second];
                        slot = slot || dep.is_public;
                    }
                }
                for (const auto& [dep_name, is_pub] : agg) {
                    t.dependencies.push_back({dep_name, is_pub});
                }
            }

            std::sort(targets.begin(), targets.end(),
                      [](const target_t& a, const target_t& b) {
                          return a.name < b.name;
                      });
            return targets;
        }

        /// @brief Напечатать таргеты (для отладки).
        void print_targets(std::ostream& os = std::cout) const {
            for (const auto& t : guess_targets()) {
                const char* kind =
                    (t.type == target_t::kind::executable)    ? "EXE" :
                    (t.type == target_t::kind::static_lib)    ? "STATIC" :
                    (t.type == target_t::kind::shared_lib)    ? "SHARED" :
                                                                "INTERFACE";
                os << t.name << " (" << kind << ")\n";
                if (!t.merged_modules.empty()) {
                    os << "  merged:";
                    for (const auto& mm : t.merged_modules)
                        os << " " << (mm.empty() ? "." : mm);
                    os << '\n';
                }
                for (const auto& s : t.sources) os << "  src: " << s << '\n';
                if (!t.public_include_dirs.empty()) {
                    os << "  -I:";
                    for (const auto& d : t.public_include_dirs)
                        os << " " << (d.empty() ? "." : d);
                    os << '\n';
                }
                if (!t.dependencies.empty()) {
                    os << "  deps:";
                    for (const auto& d : t.dependencies) {
                        os << " " << d.module;
                        if (d.is_public) os << "*";
                    }
                    os << '\n';
                }
            }
        }
};

#endif // TREE_HPP