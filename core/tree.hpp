#ifndef TREE_HPP
#define TREE_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

/**
 * @file tree.hpp
 * @brief Сканер дерева: модули, зависимости, циклы, таргеты, #finder.
 */

struct resolved_include_t {
    std::string path;
    std::string include_root;
};

struct source_dep_t {
    std::string file;
    std::vector<resolved_include_t> includes;
    bool has_main = false;
    std::vector<std::string> finder_flags; ///< #finder из этого .cpp
};

struct header_dep_t {
    std::string file;
    std::vector<resolved_include_t> includes;
    std::vector<std::string> finder_flags; ///< #finder из этого .hpp
};

struct module_dep_t {
    std::string module;
    bool is_public = false;
};

struct module_t {
    std::string dir;
    std::vector<std::string> sources;
    std::vector<std::string> headers;
    std::vector<std::string> public_headers;
    std::vector<std::string> public_include_dirs;
    std::vector<module_dep_t> dependencies;
    std::vector<std::string> extra_flags; ///< объединённые #finder из всех файлов
    bool has_main = false;

    bool is_header_only() const { return sources.empty() && !headers.empty(); }
    bool is_empty() const { return sources.empty() && headers.empty(); }
};

struct target_t {
    enum class kind { executable, static_lib, shared_lib, interface_lib };

    std::string name;
    kind type = kind::static_lib;
    std::vector<std::string> sources;
    std::vector<std::string> headers;
    std::vector<std::string> public_include_dirs;
    std::vector<module_dep_t> dependencies;
    std::vector<std::string> merged_modules;
    std::vector<std::string> extra_flags; ///< #finder-флаги (compile options)
    std::vector<std::string> extra_links; ///< #finder-флаги (-l)
    std::vector<std::string> extra_includes; ///< #finder-флаги (-I)
};

class tree_t {
    public:
        struct options_t {
            std::vector<std::string> exclude_dirs = {
                "build", "build_cmake", ".git", ".vs", ".vscode",
                "obj", "bin", "out", "project", ".pgrmake", "cmake-build-debug", "cmake-build-release"
            };
            std::vector<std::string> source_ext = {".cpp", ".cc", ".cxx", ".c"};
            std::vector<std::string> header_ext = {".hpp", ".h", ".hxx", ".hh"};
            std::vector<std::string> include_roots;
            std::vector<std::string> container_names = {
                "include", "inc", "public",
                "src", "source", "sources", "lib"
            };
            std::string project_name = "app";
            bool resolve_relative_to_file = true;
            bool resolve_relative_to_root = true;
            /// @brief Только эти .cpp (если не пусто) — белый список.
            std::vector<std::string> only_sources;
        };

    private:
        std::string root_;
        options_t opts_;
        std::vector<source_dep_t> sources_;
        std::vector<header_dep_t> headers_;
        std::map<std::string, module_t> modules_;

        static std::string normalize(const fs::path& p) {
            return p.generic_string();
        }

        static std::string parent_dir(const std::string& path) {
            auto slash = path.find_last_of('/');
            return (slash == std::string::npos) ? std::string{} : path.substr(0, slash);
        }

        static std::string last_component(const std::string& path) {
            const auto pos = path.find_last_of('/');
            return (pos == std::string::npos) ? path : path.substr(pos + 1);
        }

        static std::string safe_name(const std::string& s) {
            std::string r = s;
            for (auto& c : r) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
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
            if (i == std::string::npos || (line[i] != '"' && line[i] != '<')) return {};
            const char delim = line[i] == '<' ? '>' : '"';
            auto q2 = line.find(delim, i + 1);
            if (q2 == std::string::npos) return {};
            return line.substr(i + 1, q2 - i - 1);
        }

        /**
         * @brief Разобрать #finder-директиву в строке.
         *
         * Формат: `#finder флаг1 флаг2 ...`. Возвращает список
         * флагов или пусто, если директивы нет.
         */
        static std::vector<std::string> read_finder_flags(const std::string& line) {
            // Strict opt-in syntax: // #finder -Wall -Iinclude -lm
            // No matching inside block comments, examples or arbitrary strings.
            auto pos = line.find_first_not_of(" \t");
            if (pos == std::string::npos || line.compare(pos, 2, "//") != 0) return {};
            pos = line.find_first_not_of(" \t", pos + 2);
            if (pos == std::string::npos || line.compare(pos, 7, "#finder") != 0) return {};
            pos += 7;
            if (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') return {};
            std::istringstream in(line.substr(pos));
            std::vector<std::string> flags;
            std::string flag;
            while (in >> flag) {
                if (flag.rfind("//", 0) == 0) break;
                if (flag.size() < 2 || flag[0] != '-') return {};
                for (unsigned char c : flag)
                    if (!std::isalnum(c) && c != '-' && c != '_' && c != '=' &&
                        c != '.' && c != '/' && c != '+' && c != ':' && c != ',') return {};
                flags.push_back(flag);
            }
            return flags;
        }

        static bool is_main_definition(const std::string& line) {
            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos) return false;
            if (line[start] == '/' && start + 1 < line.size() && line[start + 1] == '/') return false;

            auto pos = line.find("main", start);
            if (pos == std::string::npos) return false;
            if (pos > 0) {
                char c = line[pos - 1];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') return false;
            }
            auto next = pos + 4;
            while (next < line.size() && (line[next] == ' ' || line[next] == '\t')) ++next;
            if (next >= line.size() || line[next] != '(') return false;
            return true;
        }

        bool is_excluded(const std::string& rel_dir) const {
            // Generated CMake trees must never be treated as project sources.
            const auto leaf = last_component(rel_dir);
            if (leaf.rfind("build-", 0) == 0 || leaf.rfind("build_", 0) == 0 ||
                leaf.rfind("cmake-build", 0) == 0 || leaf == "CMakeFiles" ||
                leaf == ".pgrmake") return true;
            for (const auto& ex : opts_.exclude_dirs) {
                if (rel_dir == ex) return true;
                if (rel_dir.size() > ex.size() &&
                    rel_dir.compare(0, ex.size() + 1, ex + "/") == 0) return true;
            }
            return false;
        }

        bool has_ext(const fs::path& p, const std::vector<std::string>& exts) const {
            const auto e = p.extension().string();
            for (const auto& x : exts) if (e == x) return true;
            return false;
        }

        bool in_only_sources(const std::string& rel) const {
            if (opts_.only_sources.empty()) return true;
            for (const auto& s : opts_.only_sources) {
                if (normalize(fs::path(s)) == rel) return true;
                std::error_code ec;
                if (fs::path(s).is_absolute()) {
                    const auto root = fs::weakly_canonical(root_, ec);
                    if (ec) continue;
                    const auto abs = fs::weakly_canonical(fs::path(s), ec);
                    if (!ec && abs == root / fs::path(rel)) return true;
                }
            }
            return false;
        }

        int scan_files() {
            std::error_code ec;
            if (!fs::is_directory(fs::u8path(root_), ec)) return 0x01;

            for (auto it = fs::recursive_directory_iterator(fs::u8path(root_), ec);
                 it != fs::recursive_directory_iterator(); ++it)
            {
                if (ec) return 0x02;
                const auto& p = it->path();
                const auto rel = fs::relative(p, fs::u8path(root_), ec);
                if (ec) continue;
                const std::string rel_norm = normalize(rel);

                if (it->is_directory()) {
                    if (is_excluded(rel_norm)) it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file()) continue;

                if (has_ext(p, opts_.source_ext)) {
                    if (!in_only_sources(rel_norm)) continue;
                    sources_.push_back({rel_norm, {}, false, {}});
                } else if (has_ext(p, opts_.header_ext)) {
                    headers_.push_back({rel_norm, {}, {}});
                }
            }
            return 0x00;
        }

        void detect_include_roots() {
            std::set<std::string> roots;
            roots.insert("");
            for (const auto& h : headers_) {
                const std::string p1 = parent_dir(h.file);
                const std::string p2 = parent_dir(p1);
                roots.insert(p1);
                roots.insert(p2);
            }
            opts_.include_roots.assign(roots.begin(), roots.end());
        }

        resolved_include_t resolve_include(const std::string& inc,
                                           const std::string& src_rel) const
        {
            std::vector<std::pair<fs::path, std::string>> candidates;
            if (opts_.resolve_relative_to_file) {
                const fs::path dir = fs::path(src_rel).parent_path();
                candidates.emplace_back(fs::u8path(root_) / dir / inc, normalize(dir));
            }
            for (const auto& r : opts_.include_roots)
                candidates.emplace_back(fs::u8path(root_) / r / inc, r);
            if (opts_.resolve_relative_to_root)
                candidates.emplace_back(fs::u8path(root_) / inc, "");

            for (const auto& [path, include_root] : candidates) {
                std::error_code ec;
                if (fs::is_regular_file(path, ec)) {
                    const auto rel = fs::relative(path, fs::u8path(root_), ec);
                    if (ec) continue;
                    return {normalize(rel), include_root};
                }
            }
            return {};
        }

        int parse_includes_of(const std::string& rel,
                              std::vector<resolved_include_t>& out,
                              bool* has_main,
                              std::vector<std::string>& finder)
        {
            std::ifstream in(fs::u8path(root_) / rel);
            if (!in) return 0x00;

            std::set<std::string> seen;
            std::string full_code;
            bool block_comment = false;
            char quote = 0;
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();

                const std::string inc = read_include_path(line);
                if (!inc.empty() && !seen.count(inc)) {
                    seen.insert(inc);
                    auto resolved = resolve_include(inc, rel);
                    if (!resolved.path.empty()) out.push_back(resolved);
                }

                // Strip both comments and string/character literals. This avoids
                // mistaking documentation such as "int main() {" for an entrypoint.
                std::string code;
                const bool preprocessor = line.find_first_not_of(" \t") != std::string::npos &&
                    line[line.find_first_not_of(" \t")] == '#';
                for (size_t i = 0; i < line.size();) {
                    if (block_comment) {
                        auto end = line.find("*/", i);
                        if (end == std::string::npos) break;
                        block_comment = false; i = end + 2;
                    } else if (quote != 0) {
                        if (line[i] == '\\' && i + 1 < line.size()) i += 2;
                        else if (line[i++] == quote) quote = 0;
                        code += ' ';
                    } else if (line.compare(i, 2, "/*") == 0) {
                        block_comment = true; i += 2;
                    } else if (line.compare(i, 2, "//") == 0) break;
                    else if (line[i] == '"' || line[i] == '\'') {
                        quote = line[i++]; code += ' ';
                    } else code += line[i++];
                }
                if (preprocessor) code.clear();
                full_code += code + "\n";

                auto flags = read_finder_flags(line);
                for (auto& f : flags) finder.push_back(std::move(f));
            }
            if (has_main) {
                static const std::regex definition(
                    R"(\b(?:int|auto)\s+main\s*\([^;{}]*\)\s*(?:->\s*int\s*)?\{)");
                *has_main = std::regex_search(full_code, definition);
            }
            return 0x00;
        }

        int parse_includes() {
            for (auto& s : sources_) {
                parse_includes_of(s.file, s.includes, &s.has_main, s.finder_flags);
            }
            for (auto& h : headers_) {
                parse_includes_of(h.file, h.includes, nullptr, h.finder_flags);
            }
            return 0x00;
        }

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
                for (const auto& f : s.finder_flags) m.extra_flags.push_back(f);
            }
            for (const auto& h : headers_) {
                auto& m = ensure(parent_dir(h.file));
                m.headers.push_back(h.file);
                for (const auto& f : h.finder_flags) m.extra_flags.push_back(f);
            }

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

            for (const auto& s : sources_) {
                const std::string from = parent_dir(s.file);
                for (const auto& inc : s.includes)
                    add_edge(from, inc.path, inc.include_root, false);
            }
            for (const auto& h : headers_) {
                const std::string from = parent_dir(h.file);
                for (const auto& inc : h.includes)
                    add_edge(from, inc.path, inc.include_root, true);
            }

            for (auto& [name, m] : modules_) {
                if (pub_headers.count(name))
                    m.public_headers.assign(pub_headers[name].begin(), pub_headers[name].end());
                if (pub_include_dirs.count(name))
                    m.public_include_dirs.assign(pub_include_dirs[name].begin(), pub_include_dirs[name].end());
                if (deps.count(name))
                    for (const auto& [to, is_pub] : deps[name])
                        m.dependencies.push_back({to, is_pub});

                // дедуплицировать extra_flags
                std::sort(m.extra_flags.begin(), m.extra_flags.end());
                m.extra_flags.erase(std::unique(m.extra_flags.begin(), m.extra_flags.end()),
                                    m.extra_flags.end());
            }
            return 0x00;
        }

    public:
        tree_t() = default;
        explicit tree_t(std::string root) : root_(std::move(root)) {}

        void set_root(std::string r) { root_ = std::move(r); }
        void set_options(const options_t& o) { opts_ = o; }
        const options_t& options() const { return opts_; }

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

        void print_summary(std::ostream& os = std::cout) const {
            os << "Root:    " << root_ << '\n'
               << "Sources: " << sources_.size() << '\n'
               << "Headers: " << headers_.size() << '\n'
               << "Modules: " << modules_.size() << '\n';
            for (const auto& [name, m] : modules_) {
                os << "\n[" << (name.empty() ? "." : name) << "]"
                   << (m.has_main ? "  [main]" : "") << '\n';
                for (const auto& s : m.sources) os << "  src: " << s << '\n';
                if (!m.extra_flags.empty()) {
                    os << "  finder:";
                    for (const auto& f : m.extra_flags) os << " " << f;
                    os << '\n';
                }
                if (!m.dependencies.empty()) {
                    os << "  deps:";
                    for (const auto& d : m.dependencies) {
                        os << " " << (d.module.empty() ? "." : d.module);
                        if (d.is_public) os << "*";
                    }
                    os << '\n';
                }
            }
        }

        std::string to_dot() const {
            std::string s;
            s += "digraph project {\n";
            s += "    rankdir=LR;\n";
            s += "    node [shape=box, style=rounded];\n";
            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;
                std::string label = (name.empty() ? "." : name);
                label += "\\n(" + std::to_string(m.sources.size()) + " src, "
                       + std::to_string(m.headers.size()) + " hdr)";
                std::string attrs;
                if (m.is_header_only()) attrs = " [style=\"rounded,dashed\"]";
                else if (m.has_main)    attrs = " [shape=doubleoctagon]";
                s += "    \"" + (name.empty() ? "." : name) + "\" [label=\"" + label + "\"]" + attrs + ";\n";
            }
            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;
                for (const auto& d : m.dependencies) {
                    const std::string style = d.is_public ? "solid" : "dashed";
                    s += "    \"" + (name.empty() ? "." : name) + "\" -> \""
                       + (d.module.empty() ? "." : d.module) + "\" [style=\"" + style + "\"];\n";
                }
            }
            s += "}\n";
            return s;
        }

        int write_dot(const std::string& path) const {
            std::ofstream out(path);
            if (!out) return 0x01;
            out << to_dot();
            return out ? 0x00 : 0x02;
        }

        std::vector<std::vector<std::string>> cycles() const {
            std::map<std::string, std::vector<std::string>> graph;
            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;
                graph[name];
                for (const auto& d : m.dependencies) graph[name].push_back(d.module);
            }

            std::map<std::string, int> index, lowlink;
            std::set<std::string> on_stack;
            std::vector<std::string> stack;
            int next_index = 0;
            std::vector<std::vector<std::string>> sccs;

            struct frame_t { std::string node; std::size_t child = 0; bool entered = false; };

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
                        if (!index.count(w)) call_stack.push_back({w, 0, false});
                        else if (on_stack.count(w)) lowlink[fr.node] = std::min(lowlink[fr.node], index[w]);
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
                        if (!call_stack.empty())
                            lowlink[call_stack.back().node] =
                                std::min(lowlink[call_stack.back().node], lowlink[done]);
                    }
                }
            }

            std::vector<std::vector<std::string>> result;
            for (auto& scc : sccs) {
                if (scc.size() > 1) result.push_back(scc);
                else if (scc.size() == 1) {
                    auto it = graph.find(scc[0]);
                    if (it != graph.end() &&
                        std::find(it->second.begin(), it->second.end(), scc[0]) != it->second.end())
                        result.push_back(scc);
                }
            }
            return result;
        }

        std::vector<target_t> guess_targets() const {
            std::map<std::string, std::vector<std::string>> groups;
            for (const auto& [name, m] : modules_) {
                if (m.is_empty()) continue;
                groups[logical_name(name)].push_back(name);
            }

            std::map<std::string, int> short_count;
            for (const auto& [logical, _] : groups) short_count[last_component(logical)]++;

            std::vector<target_t> targets;
            std::map<std::string, std::string> module_to_target;

            for (auto& [logical, dirs] : groups) {
                target_t t;
                const std::string short_name = last_component(logical);
                if (logical.empty()) t.name = opts_.project_name;
                else if (short_count[short_name] == 1) t.name = short_name;
                else t.name = safe_name(logical);

                bool has_sources = false, has_main = false;

                for (const auto& dir : dirs) {
                    const auto& m = modules_.at(dir);
                    t.merged_modules.push_back(dir);
                    module_to_target[dir] = t.name;

                    for (const auto& s : m.sources) t.sources.push_back(s);
                    for (const auto& h : m.headers) t.headers.push_back(h);
                    for (const auto& d : m.public_include_dirs) t.public_include_dirs.push_back(d);

                    // Распределить finder-флаги
                    for (const auto& f : m.extra_flags) {
                        if (f.size() > 2 && f[0] == '-' && f[1] == 'l') {
                            t.extra_links.push_back(f.substr(2));
                        } else if (f.size() > 2 && f[0] == '-' && f[1] == 'I') {
                            t.extra_includes.push_back(f.substr(2));
                        } else {
                            t.extra_flags.push_back(f);
                        }
                    }

                    has_sources = has_sources || !m.sources.empty();
                    has_main    = has_main    || m.has_main;
                }

                // dedup
                std::sort(t.extra_flags.begin(), t.extra_flags.end());
                t.extra_flags.erase(std::unique(t.extra_flags.begin(), t.extra_flags.end()), t.extra_flags.end());
                std::sort(t.extra_links.begin(), t.extra_links.end());
                t.extra_links.erase(std::unique(t.extra_links.begin(), t.extra_links.end()), t.extra_links.end());
                std::sort(t.extra_includes.begin(), t.extra_includes.end());
                t.extra_includes.erase(std::unique(t.extra_includes.begin(), t.extra_includes.end()), t.extra_includes.end());

                if (!has_sources)      t.type = target_t::kind::interface_lib;
                else if (has_main)     t.type = target_t::kind::executable;
                else                   t.type = target_t::kind::static_lib;

                targets.push_back(std::move(t));
            }

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
                for (const auto& [dep_name, is_pub] : agg)
                    t.dependencies.push_back({dep_name, is_pub});
            }

            std::sort(targets.begin(), targets.end(),
                      [](const target_t& a, const target_t& b) { return a.name < b.name; });
            return targets;
        }

        void print_targets(std::ostream& os = std::cout) const {
            for (const auto& t : guess_targets()) {
                const char* kind =
                    (t.type == target_t::kind::executable)    ? "EXE" :
                    (t.type == target_t::kind::static_lib)    ? "STATIC" :
                    (t.type == target_t::kind::shared_lib)    ? "SHARED" : "INTERFACE";
                os << t.name << " (" << kind << ")\n";
                for (const auto& s : t.sources) os << "  src: " << s << '\n';
                if (!t.extra_flags.empty()) {
                    os << "  finder:";
                    for (const auto& f : t.extra_flags) os << " " << f;
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

    private:
        std::string logical_name(const std::string& dir) const {
            if (dir.empty()) return {};
            std::vector<std::string> parts;
            std::string cur;
            for (char c : dir) {
                if (c == '/') { if (!cur.empty()) { parts.push_back(cur); cur.clear(); } }
                else cur += c;
            }
            if (!cur.empty()) parts.push_back(cur);

            std::vector<std::string> kept;
            for (const auto& p : parts) {
                bool skip = false;
                for (const auto& cn : opts_.container_names)
                    if (p == cn) { skip = true; break; }
                if (!skip) kept.push_back(p);
            }

            std::string result;
            for (std::size_t i = 0; i < kept.size(); ++i) {
                if (i > 0) result += "/";
                result += kept[i];
            }
            return result;
        }
};

#endif // TREE_HPP