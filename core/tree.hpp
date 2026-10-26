#ifndef TREE_HPP
#define TREE_HPP

#include <algorithm>
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
 * @brief Сканер дерева проекта и зависимостей между файлами.
 */

/**
 * @brief Разрешённый #include "..." внутри проекта.
 */
struct resolved_include_t {
    std::string path;          ///< Путь относительно корня проекта.
    std::string include_root;  ///< -I-корень, через который файл нашёлся.
};

/**
 * @brief Зависимости одного исходного файла.
 */
struct source_dep_t {
    std::string file;                        ///< Путь относительно корня.
    std::vector<resolved_include_t> includes; ///< Разрешённые локальные include.
};

/**
 * @brief Модуль — каталог с исходниками и/или заголовками.
 *
 * В CMake такой модуль превращается в add_executable / add_library.
 */
struct module_t {
    std::string dir;                              ///< Каталог относительно корня ("" для корня).
    std::vector<std::string> sources;             ///< .cpp файлы непосредственно в каталоге.
    std::vector<std::string> headers;             ///< .hpp/.h файлы непосредственно в каталоге.
    std::vector<std::string> public_headers;      ///< Заголовки, включаемые из других модулей.
    std::vector<std::string> public_include_dirs; ///< -I, которые нужны для публичных заголовков.
    std::vector<std::string> dependencies;        ///< Другие модули, от которых зависит этот.

    /// @brief Есть только заголовки (в CMake → INTERFACE library).
    bool is_header_only() const { return sources.empty() && !headers.empty(); }

    /// @brief Пустой модуль (не должен попадать в вывод).
    bool is_empty() const { return sources.empty() && headers.empty(); }
};

/**
 * @class tree_t
 * @brief Обходит проект и строит модель «модули + зависимости».
 *
 * Типовое использование:
 * @code
 * tree_t t(".");
 * if (t.scan() != 0x00) { ... }
 * t.print_summary();
 * // t.modules() → для генератора CMake
 * @endcode
 *
 * Модуль = каталог с исходниками или заголовками. Зависимость между
 * модулями выводится из #include "..." в исходниках: если файл из
 * модуля A включает заголовок из модуля B, то A зависит от B, а
 * соответствующий заголовок B считается публичным.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 корень не существует или не каталог
 *  - @c 0x02 ошибка обхода файловой системы
 */
class tree_t {
    public:
        /**
         * @brief Настройки сканера.
         */
        struct options_t {
            /// @brief Каталоги, которые не обходятся (build/, .git/ и т.п.).
            std::vector<std::string> exclude_dirs = {
                "build", ".git", ".vs", ".vscode", "obj", "bin", "out"
            };

            /// @brief Расширения исходников.
            std::vector<std::string> source_ext = {".cpp", ".cc", ".cxx", ".c"};

            /// @brief Расширения заголовков.
            std::vector<std::string> header_ext = {".hpp", ".h", ".hxx", ".hh"};

            /**
             * @brief Корни для -I. Если пусто — определяются автоматически
             *        из каталогов, содержащих заголовки.
             */
            std::vector<std::string> include_roots;

            /// @brief Разрешать #include относительно каталога исходника.
            bool resolve_relative_to_file = true;

            /// @brief Разрешать #include относительно корня проекта.
            bool resolve_relative_to_root = true;
        };

    private:
        std::string root_;
        options_t opts_;
        std::vector<source_dep_t> sources_;
        std::map<std::string, module_t> modules_;

        static std::string normalize(const fs::path& p) {
            return p.generic_string(); // прямые слэши на всех платформах
        }

        /**
         * @brief Разобрать строку вида `#include "path"`.
         *
         * Возвращает `path` или пустую строку, если строка — не
         * include с кавычками (системные `<...>` не интересуют).
         */
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
                    sources_.push_back({rel_norm, {}});
                }
            }
            return 0x00;
        }

        /**
         * @brief Если include_roots не заданы, собрать их из каталогов
         *        с заголовками — покрывает стандартные раскладки.
         */
        void detect_include_roots() {
            std::set<std::string> roots;
            std::error_code ec;

            for (auto it = fs::recursive_directory_iterator(root_, ec);
                 it != fs::recursive_directory_iterator(); ++it)
            {
                if (ec) return;
                const auto& p = it->path();
                const auto rel = fs::relative(p, root_, ec);
                if (ec) continue;
                const std::string rel_norm = normalize(rel);

                if (it->is_directory()) {
                    if (is_excluded(rel_norm)) it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file() && has_ext(p, opts_.header_ext)) {
                    roots.insert(normalize(fs::path(rel_norm).parent_path()));
                }
            }
            opts_.include_roots.assign(roots.begin(), roots.end());
        }

        resolved_include_t resolve_include(const std::string& inc,
                                           const std::string& src_rel) const
        {
            std::vector<std::pair<fs::path, std::string>> candidates;

            if (opts_.resolve_relative_to_file) {
                const fs::path dir = fs::path(src_rel).parent_path();
                candidates.emplace_back(fs::path(root_) / dir / inc, normalize(dir));
            }
            for (const auto& r : opts_.include_roots) {
                candidates.emplace_back(fs::path(root_) / r / inc, r);
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

        int parse_includes() {
            for (auto& src : sources_) {
                const fs::path abs = fs::path(root_) / src.file;
                std::ifstream in(abs);
                if (!in) continue;

                std::set<std::string> seen;
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    const std::string inc = read_include_path(line);
                    if (inc.empty() || seen.count(inc)) continue;
                    seen.insert(inc);

                    const auto resolved = resolve_include(inc, src.file);
                    if (!resolved.path.empty()) src.includes.push_back(resolved);
                }
            }
            return 0x00;
        }

        int build_modules() {
            // Собрать все заголовки проекта (для наполнения модулей).
            std::set<std::string> header_paths;
            {
                std::error_code ec;
                for (auto it = fs::recursive_directory_iterator(root_, ec);
                     it != fs::recursive_directory_iterator(); ++it)
                {
                    if (ec) return 0x02;
                    const auto& p = it->path();
                    const auto rel = fs::relative(p, root_, ec);
                    if (ec) continue;
                    const std::string rel_norm = normalize(rel);

                    if (it->is_directory()) {
                        if (is_excluded(rel_norm)) it.disable_recursion_pending();
                        continue;
                    }
                    if (it->is_regular_file() && has_ext(p, opts_.header_ext)) {
                        header_paths.insert(rel_norm);
                    }
                }
            }

            // Создать модули и наполнить их.
            auto ensure = [this](const std::string& dir) -> module_t& {
                auto& m = modules_[dir];
                m.dir = dir;
                return m;
            };

            for (const auto& s : sources_) {
                const std::string dir = normalize(fs::path(s.file).parent_path());
                ensure(dir).sources.push_back(s.file);
            }
            for (const auto& h : header_paths) {
                const std::string dir = normalize(fs::path(h).parent_path());
                ensure(dir).headers.push_back(h);
            }

            // Межмодульные зависимости и публичные заголовки.
            std::map<std::string, std::set<std::string>> pub_headers;
            std::map<std::string, std::set<std::string>> pub_include_dirs;
            std::map<std::string, std::set<std::string>> deps;

            for (const auto& src : sources_) {
                const std::string src_mod = normalize(fs::path(src.file).parent_path());
                for (const auto& inc : src.includes) {
                    const std::string inc_mod = normalize(fs::path(inc.path).parent_path());
                    if (inc_mod == src_mod) continue;

                    pub_headers[inc_mod].insert(inc.path);
                    pub_include_dirs[inc_mod].insert(inc.include_root);
                    deps[src_mod].insert(inc_mod);
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
                    m.dependencies.assign(deps[name].begin(),
                                          deps[name].end());
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

        /**
         * @brief Выполнить полный цикл: обход, парсинг include, сборка модулей.
         * @return @c 0x00 при успехе, коды см. в описании класса.
         */
        int scan() {
            sources_.clear();
            modules_.clear();

            int st = scan_files();
            if (st != 0x00) return st;

            if (opts_.include_roots.empty()) {
                detect_include_roots();
            }

            st = parse_includes();
            if (st != 0x00) return st;

            return build_modules();
        }

        const std::string& root() const { return root_; }
        const std::vector<source_dep_t>& sources() const { return sources_; }
        const std::map<std::string, module_t>& modules() const { return modules_; }

        /**
         * @brief Напечатать краткое описание дерева (для отладки).
         */
        void print_summary(std::ostream& os = std::cout) const {
            os << "Root:    " << root_ << '\n'
               << "Sources: " << sources_.size() << '\n'
               << "Modules: " << modules_.size() << '\n';

            for (const auto& [name, m] : modules_) {
                os << "\n[" << (name.empty() ? "." : name) << "]\n";
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
                    for (const auto& d : m.dependencies)
                        os << " " << (d.empty() ? "." : d);
                    os << '\n';
                }
            }
        }
};

#endif // TREE_HPP