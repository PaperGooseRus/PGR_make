#ifndef TUI_HPP
#define TUI_HPP

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "cmake_writer.hpp"
#include "log_t.hpp"
#include "make.hpp"
#include "read.hpp"
#include "tree.hpp"
#include "write.hpp"

/**
 * @file TUI.hpp
 * @brief Меню конфигурации, генерации CMakeLists.txt и запуска сборки.
 *
 * Персистентность:
 *  - project/config.cfg — сохранённые флаги (auto-load при старте, auto-save
 *    после каждого действия);
 *  - project/work.log   — журнал работы TUI (append, с timestamp).
 */
class tui_t {
    private:
        enum class input_result { ok, cancelled, eof };

        make_t builder_;
        flag_catalog_t catalog_;
        log_t log_;
        bool running_ = true;

        static constexpr const char* kConfigPath = "project/config.cfg";
        static constexpr const char* kLogPath    = "project/work.log";

        // ---------- Ввод ----------
        static input_result ask_line(const std::string& prompt,
                                     std::string& out,
                                     bool allow_empty = false)
        {
            std::cout << prompt;
            if (!std::getline(std::cin, out)) return input_result::eof;
            if (!allow_empty && out.empty())  return input_result::cancelled;
            return input_result::ok;
        }

        static input_result ask_int(const std::string& prompt,
                                    int min, int max, int& out)
        {
            while (true) {
                std::string s;
                auto r = ask_line(prompt, s);
                if (r != input_result::ok) return r;
                try {
                    std::size_t pos = 0;
                    int v = std::stoi(s, &pos);
                    if (pos != s.size() || v < min || v > max) {
                        std::cout << "  Введите число от " << min
                                  << " до " << max << '\n';
                        continue;
                    }
                    out = v;
                    return input_result::ok;
                } catch (...) {
                    std::cout << "  Введите число от " << min
                              << " до " << max << '\n';
                }
            }
        }

        static input_result ask_yesno(const std::string& prompt, bool& out) {
            while (true) {
                std::string s;
                auto r = ask_line(prompt + " [y/n]: ", s);
                if (r != input_result::ok) return r;
                if (s == "y" || s == "Y" || s == "yes" || s == "да") {
                    out = true;  return input_result::ok;
                }
                if (s == "n" || s == "N" || s == "no" || s == "нет") {
                    out = false; return input_result::ok;
                }
                std::cout << "  Введите y или n\n";
            }
        }

        // ---------- Отображение ----------
        static std::string disp(const std::string& v) {
            return v.empty() ? "(не задано)" : v;
        }

        static std::string disp_list(const std::vector<std::string>& v) {
            if (v.empty()) return "(не задано)";
            std::string s;
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) s += ", ";
                s += v[i];
            }
            return s;
        }

        static std::string choice_name(int choice) {
            switch (choice) {
                case 1:  return "compiler";
                case 2:  return "standard";
                case 3:  return "optimization";
                case 4:  return "debug";
                case 5:  return "warnings";
                case 6:  return "sanitizers";
                case 7:  return "lto";
                case 8:  return "arch";
                case 9:  return "specific_gcc";
                case 10: return "specific_clang";
                case 11: return "source_files";
                case 12: return "output_file";
                case 13: return "include_dirs";
                case 14: return "library_dirs";
                case 15: return "libraries";
                case 16: return "show_command";
                case 17: return "build_gpp";
                case 18: return "generate_cmake";
                case 19: return "build_cmake";
                case 20: return "save_config";
                case 21: return "load_config";
                case 0:  return "exit";
                default: return "unknown";
            }
        }

        // ---------- Редактор списка ----------
        void edit_string_list(const std::string& title,
                              std::vector<std::string>& list,
                              const std::string& add_prompt,
                              bool check_exists = false,
                              bool allow_scan = false)
        {
            while (true) {
                std::cout << "\n=== " << title << " ===\n";
                if (list.empty()) {
                    std::cout << "  (пусто)\n";
                } else {
                    for (std::size_t i = 0; i < list.size(); ++i) {
                        std::cout << " " << (i + 1) << ". " << list[i] << '\n';
                    }
                }
                std::cout << " a. Добавить\n"
                          << " d. Удалить\n"
                          << " c. Очистить\n";
                if (allow_scan) {
                    std::cout << " s. Добавить все .cpp из директории\n";
                }
                std::cout << " 0. Готово\n";

                std::string cmd;
                if (ask_line("Действие: ", cmd) != input_result::ok) return;

                if (cmd == "0") {
                    return;
                } else if (cmd == "a") {
                    std::string path;
                    if (ask_line(add_prompt, path) != input_result::ok
                        || path.empty()) continue;
                    if (make_t::has_shell_meta(path)) {
                        std::cout << "  Недопустимые символы\n";
                        continue;
                    }
                    if (check_exists && !std::filesystem::exists(path)) {
                        std::cout << "  ВНИМАНИЕ: путь не существует\n";
                        bool cont = false;
                        if (ask_yesno("Всё равно добавить?", cont)
                            != input_result::ok || !cont) continue;
                    }
                    list.push_back(path);
                } else if (cmd == "d") {
                    if (list.empty()) { std::cout << "  Список пуст\n"; continue; }
                    int idx = -1;
                    if (ask_int("Номер для удаления: ", 1,
                                static_cast<int>(list.size()), idx)
                        == input_result::ok) {
                        list.erase(list.begin() + (idx - 1));
                    }
                } else if (cmd == "c") {
                    bool ok = false;
                    if (ask_yesno("Очистить список?", ok)
                        == input_result::ok && ok) list.clear();
                } else if (cmd == "s" && allow_scan) {
                    std::string dir;
                    if (ask_line("Директория: ", dir) != input_result::ok
                        || dir.empty()) continue;
                    if (!std::filesystem::is_directory(dir)) {
                        std::cout << "  Не директория\n";
                        continue;
                    }
                    try {
                        int added = 0;
                        for (const auto& entry :
                             std::filesystem::directory_iterator(dir)) {
                            if (entry.is_regular_file() &&
                                entry.path().extension() == ".cpp") {
                                list.push_back(entry.path().string());
                                ++added;
                            }
                        }
                        std::cout << "  Добавлено: " << added << '\n';
                    } catch (const std::exception& e) {
                        std::cout << "  Ошибка: " << e.what() << '\n';
                    }
                } else {
                    std::cout << "  Неизвестная команда\n";
                }
            }
        }

        // ---------- Заголовок и меню ----------
        void print_header() const {
            const auto& c = builder_.config();
            std::cout << "\n=== Настройки сборки ===\n"
                      << "Компилятор:      " << disp(c.compiler)      << '\n'
                      << "Стандарт:        " << disp(c.standard)      << '\n'
                      << "Оптимизация:     " << disp(c.optimization)  << '\n'
                      << "Отладка:         " << disp(c.debug)         << '\n'
                      << "Предупреждения:  " << disp_list(c.warnings) << '\n'
                      << "Санитайзеры:     " << disp_list(c.sanitizers) << '\n'
                      << "LTO:             " << disp(c.lto)           << '\n'
                      << "Архитектура:     " << disp(c.arch)          << '\n'
                      << "GCC-флаги:       " << disp_list(c.specific_gcc) << '\n'
                      << "Clang-флаги:     " << disp_list(c.specific_clang) << '\n'
                      << "Include dirs:    " << disp_list(c.include_dirs) << '\n'
                      << "Library dirs:    " << disp_list(c.library_dirs) << '\n'
                      << "Libraries:       " << disp_list(c.libraries)    << '\n'
                      << "Исходники:       " << disp_list(c.source_files) << '\n'
                      << "Выход:           " << disp(c.output_file)   << '\n';
        }

        void print_menu() const {
            print_header();
            std::cout << "\n"
                      << " 1. Компилятор\n"
                      << " 2. Стандарт C++\n"
                      << " 3. Оптимизация\n"
                      << " 4. Отладка\n"
                      << " 5. Предупреждения\n"
                      << " 6. Санитайзеры\n"
                      << " 7. LTO\n"
                      << " 8. Архитектура\n"
                      << " 9. GCC-флаги\n"
                      << "10. Clang-флаги\n"
                      << "11. Исходные файлы (.cpp)\n"
                      << "12. Выходной файл\n"
                      << "13. Директории заголовков (-I)\n"
                      << "14. Директории библиотек (-L)\n"
                      << "15. Библиотеки (-l)\n"
                      << "16. Показать команду\n"
                      << "17. Собрать (одной командой g++)\n"
                      << "18. Сгенерировать CMakeLists.txt\n"
                      << "19. Собрать через CMake\n"
                      << "20. Сохранить конфиг\n"
                      << "21. Загрузить конфиг\n"
                      << " 0. Выход\n";
        }

        // ---------- Single-группы ----------
        int select_single_from(const flag_group_t& g,
                               const std::string& current)
        {
            if (g.entries.empty()) {
                std::cout << "  Нет опций (config/" << g.name << ".txt)\n";
                return -1;
            }
            std::cout << " 0. (не задано / оставить текущее)\n";
            for (std::size_t i = 0; i < g.entries.size(); ++i) {
                std::cout << " " << (i + 1) << ". " << g.entries[i].flag;
                if (!g.entries[i].description.empty())
                    std::cout << "  -- " << g.entries[i].description;
                if (g.entries[i].flag == current)
                    std::cout << "  <- текущее";
                std::cout << '\n';
            }
            int choice = -1;
            auto r = ask_int("Выбор: ", 0,
                             static_cast<int>(g.entries.size()), choice);
            if (r != input_result::ok) return -1;
            return choice;
        }

        input_result select_multi_from(const flag_group_t& g,
                                       std::vector<std::string>& current)
        {
            if (g.entries.empty()) {
                std::cout << "  Нет опций (config/" << g.name << ".txt)\n";
                return input_result::cancelled;
            }
            while (true) {
                std::cout << "\n" << g.title
                          << " (0 - готово, пусто - отмена):\n";
                for (std::size_t i = 0; i < g.entries.size(); ++i) {
                    const bool on =
                        std::find(current.begin(), current.end(),
                                  g.entries[i].flag) != current.end();
                    std::cout << " [" << (on ? "x" : " ") << "] "
                              << (i + 1) << ". " << g.entries[i].flag;
                    if (!g.entries[i].description.empty())
                        std::cout << "  -- " << g.entries[i].description;
                    std::cout << '\n';
                }
                int choice = -1;
                auto r = ask_int("Toggle: ", 0,
                                 static_cast<int>(g.entries.size()), choice);
                if (r != input_result::ok) return r;
                if (choice == 0) return input_result::ok;
                const std::string& flag = g.entries[choice - 1].flag;
                auto it = std::find(current.begin(), current.end(), flag);
                if (it != current.end()) current.erase(it);
                else                     current.push_back(flag);
            }
        }

        void do_compiler() {
            auto* g = catalog_.find("compiler");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().compiler);
            if (choice < 0) return;
            std::string val = (choice == 0) ? "g++" : g->entries[choice - 1].flag;

            std::string check_cmd = "where " + val + " >nul 2>nul";
#ifdef __linux__
            check_cmd = "command -v " + val + " >/dev/null 2>&1";
#endif
            if (std::system(check_cmd.c_str()) != 0) {
                std::cout << "  ВНИМАНИЕ: '" << val << "' не найден в PATH\n";
                bool cont = false;
                if (ask_yesno("Всё равно использовать?", cont)
                    != input_result::ok || !cont) return;
            }
            builder_.set_compiler(val);
            log_.write("[set] compiler=" + val);
        }

        void do_standard() {
            auto* g = catalog_.find("standard");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().standard);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_standard(v);
            log_.write("[set] standard=" + (v.empty() ? "(none)" : v));
        }

        void do_optimization() {
            auto* g = catalog_.find("optimization");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().optimization);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_optimization(v);
            log_.write("[set] optimization=" + (v.empty() ? "(none)" : v));
        }

        void do_debug() {
            auto* g = catalog_.find("debug");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().debug);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_debug(v);
            log_.write("[set] debug=" + (v.empty() ? "(none)" : v));
        }

        void do_lto() {
            auto* g = catalog_.find("lto");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().lto);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_lto(v);
            log_.write("[set] lto=" + (v.empty() ? "(none)" : v));
        }

        void do_arch() {
            auto* g = catalog_.find("arch");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().arch);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_arch(v);
            log_.write("[set] arch=" + (v.empty() ? "(none)" : v));
        }

        void do_warnings() {
            auto* g = catalog_.find("warnings");
            if (!g) return;
            auto cur = builder_.config().warnings;
            if (select_multi_from(*g, cur) == input_result::ok) {
                builder_.set_warnings(cur);
                log_.write("[set] warnings=" + disp_list(cur));
            }
        }

        void do_sanitizers() {
            auto* g = catalog_.find("sanitizers");
            if (!g) return;
            auto cur = builder_.config().sanitizers;
            if (select_multi_from(*g, cur) == input_result::ok) {
                auto has = [&cur](const std::string& s) {
                    return std::find(cur.begin(), cur.end(), s) != cur.end();
                };
                if (has("-fsanitize=address") && has("-fsanitize=thread")) {
                    std::cout << "  ОШИБКА: address и thread несовместимы\n";
                    log_.error("sanitizers: address+thread rejected");
                    return;
                }
                if (has("-fsanitize=address") && has("-fsanitize=memory")) {
                    std::cout << "  ОШИБКА: address и memory несовместимы\n";
                    log_.error("sanitizers: address+memory rejected");
                    return;
                }
                if (has("-fsanitize=thread") && has("-fsanitize=leak")) {
                    std::cout << "  ОШИБКА: thread и leak несовместимы\n";
                    log_.error("sanitizers: thread+leak rejected");
                    return;
                }
                builder_.set_sanitizers(cur);
                log_.write("[set] sanitizers=" + disp_list(cur));
            }
        }

        void do_specific_gcc() {
            auto* g = catalog_.find("specific_gcc");
            if (!g) return;
            auto cur = builder_.config().specific_gcc;
            if (select_multi_from(*g, cur) == input_result::ok) {
                builder_.set_specific_gcc(cur);
                log_.write("[set] specific_gcc=" + disp_list(cur));
            }
        }

        void do_specific_clang() {
            auto* g = catalog_.find("specific_clang");
            if (!g) return;
            auto cur = builder_.config().specific_clang;
            if (select_multi_from(*g, cur) == input_result::ok) {
                builder_.set_specific_clang(cur);
                log_.write("[set] specific_clang=" + disp_list(cur));
            }
        }

        void do_source_files() {
            auto cur = builder_.config().source_files;
            edit_string_list("Исходные файлы (.cpp)",
                             cur, "Путь к .cpp: ", true, true);
            builder_.set_source_files(cur);
            log_.write("[set] source_files=" + disp_list(cur));
        }

        void do_output_file() {
            std::cout << "Текущий: " << disp(builder_.config().output_file) << '\n';
            std::string v;
            if (ask_line("Новый (пусто - оставить): ", v, true)
                != input_result::ok) return;
            if (v.empty()) return;
            if (make_t::has_shell_meta(v)) {
                std::cout << "  Недопустимые символы\n";
                return;
            }
            builder_.set_output_file(v);
            log_.write("[set] output_file=" + v);
        }

        void do_include_dirs() {
            auto cur = builder_.config().include_dirs;
            edit_string_list("Директории с заголовками (-I)",
                             cur, "Путь: ", true, false);
            builder_.set_include_dirs(cur);
            log_.write("[set] include_dirs=" + disp_list(cur));
        }

        void do_library_dirs() {
            auto cur = builder_.config().library_dirs;
            edit_string_list("Директории с библиотеками (-L)",
                             cur, "Путь: ", true, false);
            builder_.set_library_dirs(cur);
            log_.write("[set] library_dirs=" + disp_list(cur));
        }

        void do_libraries() {
            auto cur = builder_.config().libraries;
            edit_string_list("Библиотеки (-l)",
                             cur,
                             "Имя (pthread) или флаг (-pthread): ",
                             false, false);
            builder_.set_libraries(cur);
            log_.write("[set] libraries=" + disp_list(cur));
        }

        void do_show_command() {
            if (builder_.config().source_files.empty()) {
                std::cout << "Сначала задайте исходные файлы (п. 11)\n";
                return;
            }
            if (builder_.build_args() != 0x00) {
                std::cout << "Ошибка конфигурации\n";
                log_.error("build_args failed (show_command)");
                return;
            }
            builder_.build_command(builder_.args());

            write_t w;
            w.write(std::string("Аргументы:"));
            w.write(builder_.args());
            w.write(std::string(""));
            w.write(std::string("Команда:"));
            w.write(builder_.command());

            log_.write("[gpp] command: " + builder_.command());
        }

        void do_build() {
            if (builder_.config().source_files.empty()) {
                std::cout << "Сначала задайте исходные файлы (п. 11)\n";
                return;
            }
            if (builder_.build_args() != 0x00) {
                std::cout << "Ошибка конфигурации\n";
                log_.error("build_args failed (build)");
                return;
            }
            builder_.build_command(builder_.args());

            std::vector<std::string> missing;
            for (const auto& src : builder_.config().source_files) {
                if (!src.empty() && !std::filesystem::exists(src))
                    missing.push_back(src);
            }
            if (!missing.empty()) {
                for (const auto& m : missing)
                    std::cout << "  ВНИМАНИЕ: не найден: " << m << '\n';
                log_.write("[gpp] missing sources: " + disp_list(missing));
                bool cont = false;
                if (ask_yesno("Продолжить?", cont) != input_result::ok || !cont) {
                    std::cout << "Отменено\n";
                    return;
                }
            }

            if (!builder_.config().output_file.empty()) {
                auto parent = std::filesystem::path(
                    builder_.config().output_file).parent_path();
                if (!parent.empty() && !std::filesystem::exists(parent)) {
                    std::error_code ec;
                    std::filesystem::create_directories(parent, ec);
                    if (ec) {
                        std::cout << "  ОШИБКА: каталог " << parent
                                  << ": " << ec.message() << '\n';
                        log_.error("cannot create " + parent.string() + ": " + ec.message());
                        return;
                    }
                }
            }

            std::cout << "Команда:\n  " << builder_.command() << '\n';
            log_.write("[gpp] execute: " + builder_.command());

            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                std::cout << "Отменено\n";
                log_.write("[gpp] cancelled by user");
                return;
            }

            int run_st = builder_.run();
            std::cout << "Статус: " << run_st << '\n';
            log_.write("[gpp] rc=" + std::to_string(run_st));
        }

        void do_generate_cmake() {
            std::cout << "Сканирование проекта...\n";
            log_.write("[cmake] scan start");

            tree_t tree(".");
            tree_t::options_t opts;
            opts.exclude_dirs = {"build", "build_cmake", ".git", ".vs",
                                 ".vscode", "obj", "bin", "out", "project"};
            opts.project_name = "PGR_make";
            tree.set_options(opts);

            if (tree.scan() != 0x00) {
                std::cout << "Ошибка сканирования проекта\n";
                log_.error("tree.scan failed");
                return;
            }

            const auto cyc = tree.cycles();
            if (!cyc.empty()) {
                std::cout << "ОБНАРУЖЕНЫ ЦИКЛЫ в зависимостях:\n";
                std::string cyc_str;
                for (const auto& c : cyc) {
                    std::cout << "  ";
                    for (const auto& m : c) {
                        std::cout << (m.empty() ? "." : m) << " ";
                        cyc_str += (m.empty() ? "." : m) + std::string(" ");
                    }
                    std::cout << '\n';
                    cyc_str += "| ";
                }
                log_.error("cycles: " + cyc_str);
                std::cout << "CMake не поддерживает циклические зависимости\n";
                return;
            }

            cmake_layout_t layout;
            if (layout.build(tree) != 0x00) {
                std::cout << "Не удалось построить план CMake\n";
                log_.error("cmake_layout.build failed");
                return;
            }

            std::cout << "\n=== Таргеты ===\n";
            tree.print_targets();

            std::cout << "\n=== План CMake ===\n";
            layout.print();

            if (std::filesystem::exists("CMakeLists.txt")) {
                bool ok = false;
                if (ask_yesno("\nCMakeLists.txt уже существует. Перезаписать?",
                              ok) != input_result::ok || !ok) {
                    std::cout << "Отменено\n";
                    log_.write("[cmake] generate cancelled");
                    return;
                }
            } else {
                bool ok = false;
                if (ask_yesno("\nЗаписать CMakeLists.txt на диск?",
                              ok) != input_result::ok || !ok) {
                    std::cout << "Отменено\n";
                    log_.write("[cmake] generate cancelled");
                    return;
                }
            }

            int st = cmake_writer_t::save(tree, layout, builder_.config());
            if (st == 0x00) {
                std::cout << "Готово. Создано файлов: "
                          << layout.files().size() << '\n';

                std::string paths;
                for (const auto& [path, _] : layout.files()) {
                    if (!paths.empty()) paths += ", ";
                    paths += path;
                }
                log_.write("[cmake] generated " +
                           std::to_string(layout.files().size()) +
                           " files: " + paths);
            } else {
                std::cout << "Ошибка записи, статус: " << st << '\n';
                log_.error("cmake_writer.save failed rc=" + std::to_string(st));
            }
        }

        void do_build_cmake() {
            if (!std::filesystem::exists("CMakeLists.txt")) {
                std::cout << "CMakeLists.txt не найден. "
                             "Сначала выполните пункт 18.\n";
                log_.error("build_cmake: CMakeLists.txt missing");
                return;
            }

            std::string build_dir;
            if (ask_line("Директория сборки (пусто = build_cmake): ",
                         build_dir, true) == input_result::eof) return;
            if (build_dir.empty()) build_dir = "build_cmake";

            std::string generator_opt;
            std::string check = "where ninja >nul 2>nul";
#ifdef __linux__
            check = "command -v ninja >/dev/null 2>&1";
#endif
            if (std::system(check.c_str()) == 0) {
                bool use_ninja = false;
                if (ask_yesno("Использовать Ninja (быстрее)?",
                              use_ninja) == input_result::ok && use_ninja) {
                    generator_opt = " -G Ninja";
                }
            }

            std::string configure_cmd =
                "cmake -S . -B " + build_dir + generator_opt
                + " -DCMAKE_CXX_COMPILER=" + builder_.config().compiler;
            std::string build_cmd = "cmake --build " + build_dir;

            std::cout << "Configure: " << configure_cmd << '\n';
            std::cout << "Build:     " << build_cmd << '\n';

            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                std::cout << "Отменено\n";
                log_.write("[cmake] build cancelled");
                return;
            }

            log_.write("[cmake] configure: " + configure_cmd);
            std::cout << "\n--- cmake configure ---\n";
            int rc1 = std::system(configure_cmd.c_str());
            log_.write("[cmake] configure rc=" + std::to_string(rc1));
            if (rc1 != 0) {
                std::cout << "Configure failed, rc = " << rc1 << '\n';
                return;
            }

            log_.write("[cmake] build: " + build_cmd);
            std::cout << "\n--- cmake build ---\n";
            int rc2 = std::system(build_cmd.c_str());
            log_.write("[cmake] build rc=" + std::to_string(rc2));
            if (rc2 != 0) {
                std::cout << "Build failed, rc = " << rc2 << '\n';
                return;
            }

            std::cout << "Готово\n";
        }

        void do_save_config() {
            int st = builder_.save(kConfigPath);
            if (st == 0x00) {
                std::cout << "Сохранено в " << kConfigPath << '\n';
                log_.write("[config] saved to " + std::string(kConfigPath));
            } else {
                std::cout << "Ошибка сохранения, статус: " << st << '\n';
                log_.error("save_config rc=" + std::to_string(st));
            }
        }

        void do_load_config() {
            int st = builder_.load(kConfigPath);
            if (st == 0x00) {
                std::cout << "Загружено из " << kConfigPath << '\n';
                log_.write("[config] loaded from " + std::string(kConfigPath));
            } else if (st == 0x04) {
                std::cout << "Файл " << kConfigPath
                          << " не найден — сначала сохраните конфиг\n";
                log_.write("[config] load: file not found");
            } else {
                std::cout << "Ошибка загрузки, статус: " << st << '\n';
                log_.error("load_config rc=" + std::to_string(st));
            }
        }

        void handle(int choice) {
            log_.write("[menu] " + std::to_string(choice) +
                       " (" + choice_name(choice) + ")");

            switch (choice) {
                case 1:  do_compiler();       break;
                case 2:  do_standard();       break;
                case 3:  do_optimization();   break;
                case 4:  do_debug();          break;
                case 5:  do_warnings();       break;
                case 6:  do_sanitizers();     break;
                case 7:  do_lto();            break;
                case 8:  do_arch();           break;
                case 9:  do_specific_gcc();   break;
                case 10: do_specific_clang(); break;
                case 11: do_source_files();   break;
                case 12: do_output_file();    break;
                case 13: do_include_dirs();   break;
                case 14: do_library_dirs();   break;
                case 15: do_libraries();      break;
                case 16: do_show_command();   break;
                case 17: do_build();          break;
                case 18: do_generate_cmake(); break;
                case 19: do_build_cmake();    break;
                case 20: do_save_config();    break;
                case 21: do_load_config();    break;
                case 0:  running_ = false;    break;
                default: std::cout << "Нет такого пункта\n"; break;
            }
        }

    public:
        tui_t() = default;
        ~tui_t() = default;

        void run() {
            std::error_code ec;
            std::filesystem::create_directories("build", ec);
            std::filesystem::create_directories("project", ec);
            if (ec) {
                std::cerr << "Не удалось создать project/: "
                          << ec.message() << '\n';
                return;
            }

            if (log_.open(kLogPath) != 0x00) {
                std::cerr << "Не удалось открыть лог " << kLogPath
                          << " — продолжаю без логирования\n";
            }
            log_.write("=== Session started ===");

            if (catalog_.load("config") != 0x00) {
                std::cerr << "Не удалось загрузить ни одного файла из config/\n";
                log_.error("catalog.load failed");
                return;
            }
            log_.write("[catalog] loaded from config/");

            if (builder_.load(kConfigPath) != 0x00) {
                std::cout << "(конфиг не найден — применены значения по умолчанию)\n";
                log_.write("[config] not found, applying defaults");

                builder_.set_compiler("g++");
                builder_.set_standard("-std=c++17");
                builder_.set_optimization("-Os");
                builder_.set_warnings({"-Wall", "-Wextra"});
                builder_.set_specific_gcc({"-fno-declone-ctor-dtor"});
                builder_.set_source_files({"main.cpp"});

                builder_.save(kConfigPath);
            } else {
                std::cout << "(конфиг загружен из " << kConfigPath << ")\n";
                log_.write("[config] loaded from " + std::string(kConfigPath));
            }

            while (running_) {
                print_menu();

                int choice = -1;
                auto r = ask_int("Выбор: ", 0, 21, choice);
                if (r == input_result::eof) {
                    std::cout << "\n(EOF - выход)\n";
                    log_.write("[menu] EOF");
                    break;
                }
                if (r == input_result::cancelled) continue;

                handle(choice);

                builder_.save(kConfigPath);
            }

            builder_.save(kConfigPath);
            log_.write("=== Session ended ===");
            std::cout << "Настройки сохранены в " << kConfigPath << '\n';
            std::cout << "Лог: " << kLogPath << '\n';
            std::cout << "До встречи!\n";
        }
};

#endif // TUI_HPP