#ifndef TUI_HPP
#define TUI_HPP

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "cmake_writer.hpp"
#include "make.hpp"
#include "read.hpp"
#include "tree.hpp"
#include "write.hpp"

/**
 * @file TUI.hpp
 * @brief Меню конфигурации, генерации CMakeLists.txt и запуска сборки.
 */

class tui_t {
    private:
        enum class input_result { ok, cancelled, eof };

        make_t builder_;
        flag_catalog_t catalog_;
        bool running_ = true;

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
        }

        void do_standard() {
            auto* g = catalog_.find("standard");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().standard);
            if (c < 0) return;
            builder_.set_standard(c == 0 ? "" : g->entries[c - 1].flag);
        }

        void do_optimization() {
            auto* g = catalog_.find("optimization");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().optimization);
            if (c < 0) return;
            builder_.set_optimization(c == 0 ? "" : g->entries[c - 1].flag);
        }

        void do_debug() {
            auto* g = catalog_.find("debug");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().debug);
            if (c < 0) return;
            builder_.set_debug(c == 0 ? "" : g->entries[c - 1].flag);
        }

        void do_lto() {
            auto* g = catalog_.find("lto");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().lto);
            if (c < 0) return;
            builder_.set_lto(c == 0 ? "" : g->entries[c - 1].flag);
        }

        void do_arch() {
            auto* g = catalog_.find("arch");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().arch);
            if (c < 0) return;
            builder_.set_arch(c == 0 ? "" : g->entries[c - 1].flag);
        }

        void do_warnings() {
            auto* g = catalog_.find("warnings");
            if (!g) return;
            auto cur = builder_.config().warnings;
            if (select_multi_from(*g, cur) == input_result::ok)
                builder_.set_warnings(cur);
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
                    return;
                }
                if (has("-fsanitize=address") && has("-fsanitize=memory")) {
                    std::cout << "  ОШИБКА: address и memory несовместимы\n";
                    return;
                }
                if (has("-fsanitize=thread") && has("-fsanitize=leak")) {
                    std::cout << "  ОШИБКА: thread и leak несовместимы\n";
                    return;
                }
                builder_.set_sanitizers(cur);
            }
        }

        void do_specific_gcc() {
            auto* g = catalog_.find("specific_gcc");
            if (!g) return;
            auto cur = builder_.config().specific_gcc;
            if (select_multi_from(*g, cur) == input_result::ok)
                builder_.set_specific_gcc(cur);
        }

        void do_specific_clang() {
            auto* g = catalog_.find("specific_clang");
            if (!g) return;
            auto cur = builder_.config().specific_clang;
            if (select_multi_from(*g, cur) == input_result::ok)
                builder_.set_specific_clang(cur);
        }

        void do_source_files() {
            auto cur = builder_.config().source_files;
            edit_string_list("Исходные файлы (.cpp)",
                             cur, "Путь к .cpp: ", true, true);
            builder_.set_source_files(cur);
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
        }

        void do_include_dirs() {
            auto cur = builder_.config().include_dirs;
            edit_string_list("Директории с заголовками (-I)",
                             cur, "Путь: ", true, false);
            builder_.set_include_dirs(cur);
        }

        void do_library_dirs() {
            auto cur = builder_.config().library_dirs;
            edit_string_list("Директории с библиотеками (-L)",
                             cur, "Путь: ", true, false);
            builder_.set_library_dirs(cur);
        }

        void do_libraries() {
            auto cur = builder_.config().libraries;
            edit_string_list("Библиотеки (-l)",
                             cur,
                             "Имя (pthread) или флаг (-pthread): ",
                             false, false);
            builder_.set_libraries(cur);
        }

        // ---------- Сборка одной командой ----------
        void do_show_command() {
            if (builder_.config().source_files.empty()) {
                std::cout << "Сначала задайте исходные файлы (п. 11)\n";
                return;
            }
            if (builder_.build_args() != 0x00) {
                std::cout << "Ошибка конфигурации\n";
                return;
            }
            builder_.build_command(builder_.args());

            write_t w;
            w.write(std::string("Аргументы:"));
            w.write(builder_.args());
            w.write(std::string(""));
            w.write(std::string("Команда:"));
            w.write(builder_.command());
        }

        void do_build() {
            if (builder_.config().source_files.empty()) {
                std::cout << "Сначала задайте исходные файлы (п. 11)\n";
                return;
            }
            if (builder_.build_args() != 0x00) {
                std::cout << "Ошибка конфигурации\n";
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
                        return;
                    }
                }
            }

            std::cout << "Команда:\n  " << builder_.command() << '\n';

            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                std::cout << "Отменено\n";
                return;
            }

            int run_st = builder_.run();
            std::cout << "Статус: " << run_st << '\n';
        }

        // ---------- CMake: генерация и сборка ----------

        void do_generate_cmake() {
            std::cout << "Сканирование проекта...\n";

            tree_t tree(".");
            tree.set_options({
                .exclude_dirs = {"build", ".git", ".vs", ".vscode",
                                 "obj", "bin", "out"},
                .project_name = "app",
            });

            if (tree.scan() != 0x00) {
                std::cout << "Ошибка сканирования проекта\n";
                return;
            }

            const auto cyc = tree.cycles();
            if (!cyc.empty()) {
                std::cout << "ОБНАРУЖЕНЫ ЦИКЛЫ в зависимостях:\n";
                for (const auto& c : cyc) {
                    std::cout << "  ";
                    for (const auto& m : c)
                        std::cout << (m.empty() ? "." : m) << " ";
                    std::cout << '\n';
                }
                std::cout << "CMake не поддерживает циклические зависимости\n";
                return;
            }

            cmake_layout_t layout;
            if (layout.build(tree) != 0x00) {
                std::cout << "Не удалось построить план CMake\n";
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
                    return;
                }
            } else {
                bool ok = false;
                if (ask_yesno("\nЗаписать CMakeLists.txt на диск?",
                              ok) != input_result::ok || !ok) {
                    std::cout << "Отменено\n";
                    return;
                }
            }

            int st = cmake_writer_t::save(tree, layout, builder_.config());
            if (st == 0x00) {
                std::cout << "Готово. Создано файлов: "
                          << layout.files().size() << '\n';
            } else {
                std::cout << "Ошибка записи, статус: " << st << '\n';
            }
        }

        void do_build_cmake() {
            if (!std::filesystem::exists("CMakeLists.txt")) {
                std::cout << "CMakeLists.txt не найден. "
                             "Сначала выполните пункт 18.\n";
                return;
            }

            std::string build_dir;
            if (ask_line("Директория сборки (пусто = build): ",
                         build_dir, true) == input_result::eof) return;
            if (build_dir.empty()) build_dir = "build";

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
                return;
            }

            std::cout << "\n--- cmake configure ---\n";
            int rc1 = std::system(configure_cmd.c_str());
            if (rc1 != 0) {
                std::cout << "Configure failed, rc = " << rc1 << '\n';
                return;
            }

            std::cout << "\n--- cmake build ---\n";
            int rc2 = std::system(build_cmd.c_str());
            if (rc2 != 0) {
                std::cout << "Build failed, rc = " << rc2 << '\n';
                return;
            }

            std::cout << "Готово\n";
        }

        // ---------- Диспетчер ----------
        void handle(int choice) {
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
            if (ec) {
                std::cerr << "Не удалось создать build/: "
                          << ec.message() << '\n';
                return;
            }

            if (catalog_.load("config") != 0x00) {
                std::cerr << "Не удалось загрузить ни одного файла из config/\n";
                return;
            }

            while (running_) {
                print_menu();

                int choice = -1;
                auto r = ask_int("Выбор: ", 0, 19, choice);
                if (r == input_result::eof) {
                    std::cout << "\n(EOF - выход)\n";
                    break;
                }
                if (r == input_result::cancelled) continue;

                handle(choice);
            }

            std::cout << "До встречи!\n";
        }
};

#endif // TUI_HPP