#ifndef TUI_HPP
#define TUI_HPP

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

#include "cmake_writer.hpp"
#include "log_t.hpp"
#include "make.hpp"
#include "process_runner.hpp"
#include "safe_paths.hpp"
#include "read.hpp"
#include "tree.hpp"
#include "write.hpp"

/**
 * @file TUI.hpp
 * @brief Меню конфигурации, генерации CMakeLists.txt и запуска сборки.
 *
 * UX:
 *  - полная перерисовка экрана после каждого действия;
 *  - компактная сводка в шапке;
 *  - статус-строка с результатом последнего действия;
 *  - группы: настройки / действия / пакеты / сервис / выход.
 *
 * Персистентность:
 *  - project/config.cfg — сохранённые флаги (auto-load, auto-save);
 *  - project/profiles/NAME.cfg — альтернативные профили;
 *  - project/work.log — журнал (append, с timestamp).
 */
class tui_t {
    private:
        enum class input_result { ok, cancelled, eof };

        make_t builder_;
        flag_catalog_t catalog_;
        log_t log_;
        bool running_ = true;

        std::string status_;
        bool status_is_error_ = false;

        static constexpr const char* kConfigPath  = "project/config.cfg";
        static constexpr const char* kLogPath     = "project/work.log";
        static constexpr const char* kProfilesDir = "project/profiles";
        static constexpr const char* kTitle       = "PGR_make";

        // ------------------------------------------------------------------
        // Экран
        // ------------------------------------------------------------------

#ifdef _WIN32
        static void clear_screen() {
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (!GetConsoleScreenBufferInfo(h, &info)) {
                std::cout << "\033[2J\033[H" << std::flush;
                return;
            }
            DWORD cells = info.dwSize.X * info.dwSize.Y;
            DWORD written = 0;
            COORD origin = {0, 0};
            FillConsoleOutputCharacterA(h, ' ', cells, origin, &written);
            FillConsoleOutputAttribute(h, info.wAttributes, cells, origin, &written);
            SetConsoleCursorPosition(h, origin);
        }
#else
        static void clear_screen() {
            std::cout << "\033[2J\033[H" << std::flush;
        }
#endif

        static void wait_enter() {
            std::cout << "\n-- Enter --" << std::flush;
            std::string line;
            std::getline(std::cin, line);
        }

        static std::size_t display_width(const std::string& s) {
            std::size_t w = 0;
            for (std::size_t i = 0; i < s.size(); ) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if      (c < 0x80)           { ++i; ++w; }
                else if ((c & 0xE0) == 0xC0) { i += 2; ++w; }
                else if ((c & 0xF0) == 0xE0) { i += 3; ++w; }
                else if ((c & 0xF8) == 0xF0) { i += 4; ++w; }
                else                         { ++i; ++w; }
            }
            return w;
        }

        static std::string pad_right(const std::string& s, std::size_t width) {
            const std::size_t w = display_width(s);
            if (w >= width) return s;
            return s + std::string(width - w, ' ');
        }

        static void print_sep(char c = '-', std::size_t width = 74) {
            std::cout << std::string(width, c) << '\n';
        }

        void set_status(const std::string& msg, bool is_error = false) {
            status_ = msg;
            status_is_error_ = is_error;
        }

        // ------------------------------------------------------------------
        // Отображение
        // ------------------------------------------------------------------

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

        void print_summary_line() const {
            const auto& c = builder_.config();

            print_sep('=', 74);
            std::string title = std::string(kTitle);
            if (!c.profile.empty()) title += "  [" + c.profile + "]";
            std::cout << " " << title << '\n';
            print_sep('=', 74);

            std::string s;
            auto append = [&s](const std::string& v) {
                if (v.empty()) return;
                if (!s.empty()) s += " | ";
                s += v;
            };

            append(c.compiler);
            append(c.standard);
            append(c.optimization);
            append(c.debug);
            if (!c.lto.empty())  append(c.lto);
            if (!c.arch.empty()) append(c.arch);

            const std::size_t flags =
                c.warnings.size() + c.sanitizers.size()
              + c.specific_gcc.size() + c.specific_clang.size()
              + c.include_dirs.size() + c.library_dirs.size()
              + c.libraries.size();
            if (flags > 0) append("opt:" + std::to_string(flags));
            if (!c.packages.empty())
                append("pkg:" + std::to_string(c.packages.size()));
            if (!c.source_files.empty())
                append("src:" + std::to_string(c.source_files.size()));

            std::cout << " " << (s.empty() ? "(по умолчанию)" : s) << '\n';
            print_sep('-', 74);
        }

        void print_status_line() {
            if (status_.empty()) return;
            std::cout << " Статус: "
                      << (status_is_error_ ? "[!] " : "")
                      << status_ << '\n';
            status_.clear();
            status_is_error_ = false;
        }

        static void print_row(int num, const std::string& label,
                              const std::string& value = "")
        {
            std::cout << (num < 10 ? "  " : " ")
                      << num << ". " << pad_right(label, 28) << value << '\n';
        }

        void print_body() const {
            const auto& c = builder_.config();

            std::cout << '\n';
            print_row(1,  "Компилятор",     c.compiler);
            print_row(2,  "Стандарт",       disp(c.standard));
            print_row(3,  "Оптимизация",    disp(c.optimization));
            print_row(4,  "Отладка",        disp(c.debug));
            print_row(5,  "Предупреждения", disp_list(c.warnings));
            print_row(6,  "Санитайзеры",    disp_list(c.sanitizers));
            print_row(7,  "LTO",            disp(c.lto));
            print_row(8,  "Архитектура",    disp(c.arch));
            print_row(9,  "GCC-флаги",      disp_list(c.specific_gcc));
            print_row(10, "Clang-флаги",    disp_list(c.specific_clang));
            print_row(11, "Исходники",      disp_list(c.source_files));
            print_row(12, "Выходной файл",  disp(c.output_file));
            print_row(13, "Include dirs",   disp_list(c.include_dirs));
            print_row(14, "Library dirs",   disp_list(c.library_dirs));
            print_row(15, "Библиотеки",     disp_list(c.libraries));

            print_sep('-', 74);

            print_row(16, "Показать команду");
            print_row(17, "Собрать (g++)");
            print_row(18, "Сгенерировать CMakeLists.txt");
            print_row(19, "Собрать через CMake");
            print_row(20, "Сохранить конфиг");
            print_row(21, "Загрузить конфиг");

            print_sep('-', 74);

            print_row(22, "Пакеты (find_package)", disp_list(c.packages));
            print_row(23, "Профиль",               disp(c.profile));
            print_row(24, "Очистить build_cmake");
            print_row(25, "compile_commands.json → корень");
            print_row(26, "Toolchain file (CMake)", disp(c.toolchain_file));
            print_row(27, "Стандарт C", disp(c.c_standard));
            print_row(28, "Linker script (.ld)", disp(c.linker_script));
            print_row(29, "Тип библиотек", disp(c.target_type));

            print_sep('-', 74);
            print_row(0,  "Выход");
        }

        void print_menu() {
            clear_screen();
            print_summary_line();
            print_status_line();
            print_body();
        }

        // ------------------------------------------------------------------
        // Ввод
        // ------------------------------------------------------------------

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

        // ------------------------------------------------------------------
        // Редактор списка строк
        // ------------------------------------------------------------------

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

        // ------------------------------------------------------------------
        // Выбор из каталога флагов
        // ------------------------------------------------------------------

        int select_single_from(const flag_group_t& g,
                               const std::string& current)
        {
            if (g.entries.empty()) {
                std::cout << "  Нет опций (config/" << g.name << ".txt)\n";
                return -1;
            }
            std::cout << " 0. (сбросить значение)\n";
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

        // ------------------------------------------------------------------
        // Действия: single-группы (1-8)
        // ------------------------------------------------------------------

        void do_compiler() {
            auto* g = catalog_.find("compiler");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().compiler);
            if (choice < 0) return;
            std::string val = (choice == 0) ? "g++" : g->entries[choice - 1].flag;

            if (!pgr_process::available(val)) {
                std::cout << "  ВНИМАНИЕ: '" << val << "' не найден в PATH\n";
                bool cont = false;
                if (ask_yesno("Всё равно использовать?", cont)
                    != input_result::ok || !cont) {
                    set_status("Отменено");
                    return;
                }
            }
            builder_.set_compiler(val);
            log_.write("[set] compiler=" + val);
            set_status("Компилятор: " + val);
        }

        void do_standard() {
            auto* g = catalog_.find("standard");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().standard);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_standard(v);
            log_.write("[set] standard=" + (v.empty() ? "(none)" : v));
            set_status("Стандарт: " + disp(v));
        }

        void do_optimization() {
            auto* g = catalog_.find("optimization");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().optimization);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_optimization(v);
            log_.write("[set] optimization=" + (v.empty() ? "(none)" : v));
            set_status("Оптимизация: " + disp(v));
        }

        void do_debug() {
            auto* g = catalog_.find("debug");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().debug);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_debug(v);
            log_.write("[set] debug=" + (v.empty() ? "(none)" : v));
            set_status("Отладка: " + disp(v));
        }

        void do_warnings() {
            auto* g = catalog_.find("warnings");
            if (!g) return;
            auto cur = builder_.config().warnings;
            if (select_multi_from(*g, cur) == input_result::ok) {
                builder_.set_warnings(cur);
                log_.write("[set] warnings=" + disp_list(cur));
                set_status("Предупреждения: " + disp_list(cur));
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
                    log_.error("sanitizers: address+thread rejected");
                    set_status("Ошибка: address и thread несовместимы", true);
                    return;
                }
                if (has("-fsanitize=address") && has("-fsanitize=memory")) {
                    log_.error("sanitizers: address+memory rejected");
                    set_status("Ошибка: address и memory несовместимы", true);
                    return;
                }
                if (has("-fsanitize=thread") && has("-fsanitize=leak")) {
                    log_.error("sanitizers: thread+leak rejected");
                    set_status("Ошибка: thread и leak несовместимы", true);
                    return;
                }
                builder_.set_sanitizers(cur);
                log_.write("[set] sanitizers=" + disp_list(cur));
                set_status("Санитайзеры: " + disp_list(cur));
            }
        }

        void do_lto() {
            auto* g = catalog_.find("lto");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().lto);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_lto(v);
            log_.write("[set] lto=" + (v.empty() ? "(none)" : v));
            set_status("LTO: " + disp(v));
        }

        void do_arch() {
            auto* g = catalog_.find("arch");
            if (!g) return;
            int c = select_single_from(*g, builder_.config().arch);
            if (c < 0) return;
            std::string v = (c == 0) ? "" : g->entries[c - 1].flag;
            builder_.set_arch(v);
            log_.write("[set] arch=" + (v.empty() ? "(none)" : v));
            set_status("Архитектура: " + disp(v));
        }

        // ------------------------------------------------------------------
        // Действия: специфичные флаги (9-10)
        // ------------------------------------------------------------------

        void do_specific_gcc() {
            auto* g = catalog_.find("specific_gcc");
            if (!g) return;
            auto cur = builder_.config().specific_gcc;
            if (select_multi_from(*g, cur) == input_result::ok) {
                builder_.set_specific_gcc(cur);
                log_.write("[set] specific_gcc=" + disp_list(cur));
                set_status("GCC-флаги: " + disp_list(cur));
            }
        }

        void do_specific_clang() {
            auto* g = catalog_.find("specific_clang");
            if (!g) return;
            auto cur = builder_.config().specific_clang;
            if (select_multi_from(*g, cur) == input_result::ok) {
                builder_.set_specific_clang(cur);
                log_.write("[set] specific_clang=" + disp_list(cur));
                set_status("Clang-флаги: " + disp_list(cur));
            }
        }

        // ------------------------------------------------------------------
        // Действия: пути и библиотеки (11-15)
        // ------------------------------------------------------------------

        void do_source_files() {
            auto cur = builder_.config().source_files;
            edit_string_list("Исходные файлы (.cpp)",
                             cur, "Путь к .cpp: ", true, true);
            builder_.set_source_files(cur);
            log_.write("[set] source_files=" + disp_list(cur));
            set_status("Исходники: " + std::to_string(cur.size()) + " файл(ов)");
        }

        void do_output_file() {
            std::cout << "Текущий: " << disp(builder_.config().output_file) << '\n';
            std::string v;
            if (ask_line("Новый (пусто - оставить): ", v, true)
                != input_result::ok) return;
            if (v.empty()) return;
            if (make_t::has_shell_meta(v)) {
                set_status("Недопустимые символы в пути", true);
                return;
            }
            builder_.set_output_file(v);
            log_.write("[set] output_file=" + v);
            set_status("Выходной файл: " + v);
        }

        void do_include_dirs() {
            auto cur = builder_.config().include_dirs;
            edit_string_list("Директории с заголовками (-I)",
                             cur, "Путь: ", true, false);
            builder_.set_include_dirs(cur);
            log_.write("[set] include_dirs=" + disp_list(cur));
            set_status("Include dirs: " + disp_list(cur));
        }

        void do_library_dirs() {
            auto cur = builder_.config().library_dirs;
            edit_string_list("Директории с библиотеками (-L)",
                             cur, "Путь: ", true, false);
            builder_.set_library_dirs(cur);
            log_.write("[set] library_dirs=" + disp_list(cur));
            set_status("Library dirs: " + disp_list(cur));
        }

        void do_libraries() {
            auto cur = builder_.config().libraries;
            edit_string_list("Библиотеки (-l)",
                             cur,
                             "Имя (pthread) или флаг (-pthread): ",
                             false, false);
            builder_.set_libraries(cur);
            log_.write("[set] libraries=" + disp_list(cur));
            set_status("Библиотеки: " + disp_list(cur));
        }

        // ------------------------------------------------------------------
        // Действия: показать команду / собрать g++ (16-17)
        // ------------------------------------------------------------------

        void do_show_command() {
            if (builder_.config().source_files.empty()) {
                set_status("Сначала задайте исходные файлы (п. 11)", true);
                return;
            }
            if (builder_.build_args() != 0x00) {
                log_.error("build_args failed (show_command)");
                set_status("Ошибка конфигурации (см. project/work.log)", true);
                return;
            }
            builder_.build_command(builder_.args());

            std::cout << "\nАргументы:\n";
            for (const auto& a : builder_.args()) std::cout << "  " << a << '\n';
            std::cout << "\nКоманда:\n  " << builder_.command() << '\n';

            log_.write("[gpp] command: " + builder_.command());
            set_status("Команда собрана");
            wait_enter();
        }

        void do_build() {
            if (builder_.config().source_files.empty()) {
                set_status("Сначала задайте исходные файлы (п. 11)", true);
                return;
            }
            if (builder_.build_args() != 0x00) {
                log_.error("build_args failed (build)");
                set_status("Ошибка конфигурации (см. project/work.log)", true);
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
                log_.write("[gpp] missing: " + disp_list(missing));
                bool cont = false;
                if (ask_yesno("Продолжить?", cont) != input_result::ok || !cont) {
                    set_status("Отменено");
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
                        log_.error("cannot create " + parent.string() + ": " + ec.message());
                        set_status("Не удалось создать " + parent.string(), true);
                        return;
                    }
                }
            }

            std::cout << "\nКоманда:\n  " << builder_.command() << '\n';
            log_.write("[gpp] execute: " + builder_.command());

            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                set_status("Отменено");
                return;
            }

            std::cout << '\n';
            int rc = builder_.run([this](const std::string& line) {
                log_.write("[gpp] " + line);
            });
            log_.write("[gpp] rc=" + std::to_string(rc));

            if (rc == 0x00) set_status("Сборка (g++): успех");
            else            set_status("Сборка (g++): ошибка, rc=" + std::to_string(rc), true);
            wait_enter();
        }

        // ------------------------------------------------------------------
        // Действия: CMake (18-19)
        // ------------------------------------------------------------------

        void do_generate_cmake() {
            std::cout << "Сканирование проекта...\n";
            log_.write("[cmake] scan start");

            tree_t tree(".");
            tree_t::options_t opts;
            opts.exclude_dirs = {"build", "build_cmake", ".git", ".vs",
                                 ".vscode", "obj", "bin", "out", "project"};
            opts.project_name = "PGR_make";
            opts.include_roots = builder_.config().include_dirs;
            // Белый список: если source_files заданы в конфиге — уважать.
            // Если пусто — сканировать всё дерево.
            if (!builder_.config().source_files.empty()) {
                opts.only_sources = builder_.config().source_files;
            }
            tree.set_options(opts);

            if (tree.scan() != 0x00) {
                log_.error("tree.scan failed");
                set_status("Ошибка сканирования", true);
                return;
            }

            const auto cyc = tree.cycles();
            if (!cyc.empty()) {
                std::cout << "Предупреждение: обнаружены циклы включения заголовков; "
                             "генерация будет продолжена.\n";
                log_.write("[cmake] header/module include cycles detected");
            }

            if (builder_.validate_configuration() != 0) {
                set_status("Некорректные параметры CMake", true);
                return;
            }
            cmake_layout_t layout;
            int lst = layout.build(tree);
            if (lst == 0x01) {
                log_.error("cmake_layout: cycles");
                set_status("Циклы в зависимостях — CMake невозможен", true);
                wait_enter();
                return;
            }
            if (lst == 0x03) {
                log_.error("cmake_layout: subdir cycle");
                set_status("Цикл в порядке add_subdirectory", true);
                wait_enter();
                return;
            }
            if (lst != 0x00) {
                log_.error("cmake_layout.build failed");
                set_status("Не удалось построить план", true);
                return;
            }

            std::cout << "\n=== Таргеты ===\n";
            tree.print_targets();
            std::cout << "\n=== План CMake ===\n";
            layout.print();

            std::cout << "\nГенерация в " << cmake_writer_t::staged_root
                      << " (существующие исходные CMakeLists.txt не изменяются).\n";
            bool ok = false;
            if (ask_yesno("Сгенерировать проект CMake?", ok) != input_result::ok || !ok) {
                set_status("Отменено");
                return;
            }
            int st = cmake_writer_t::save_staged(tree, layout, builder_.config());
            if (st == 0x00) {
                std::string paths;
                for (const auto& [p, _] : layout.files()) {
                    if (!paths.empty()) paths += ", ";
                    paths += p;
                }
                log_.write("[cmake] generated " +
                           std::to_string(layout.files().size()) +
                           " files: " + paths);
                std::cout << "\nГотово. Создано файлов: "
                          << layout.files().size() << '\n';
                set_status("Создано файлов: " + std::to_string(layout.files().size()));
                wait_enter();
            } else {
                log_.error("cmake_writer.save failed rc=" + std::to_string(st));
                set_status("Ошибка записи, rc=" + std::to_string(st), true);
                wait_enter();
            }
        }

        void do_build_cmake() {
            if (!std::filesystem::exists(
                std::filesystem::path(cmake_writer_t::staged_root) / "CMakeLists.txt")) {
                set_status("Сначала сгенерируйте CMake (п. 18)", true);
                return;
            }
            std::string build_dir;
            if (ask_line("Директория сборки (пусто = build_cmake): ",
                         build_dir, true) == input_result::eof) return;
            if (build_dir.empty()) build_dir = "build_cmake";
            if (!pgr_paths::safe_build_dir(build_dir)) {
                set_status("Недопустимый каталог сборки: " + build_dir, true);
                return;
            }
            std::vector<std::string> configure = {
                "cmake", "-S", cmake_writer_t::staged_root, "-B", build_dir
            };
            if (!std::filesystem::exists(std::filesystem::path(build_dir) / "CMakeCache.txt") &&
                pgr_process::available("ninja")) {
                bool use_ninja = false;
                if (ask_yesno("Использовать Ninja?", use_ninja)
                    == input_result::ok && use_ninja) {
                    configure.push_back("-G");
                    configure.push_back("Ninja");
                }
            }
            if (builder_.config().toolchain_file.empty()) {
                configure.push_back("-DCMAKE_CXX_COMPILER=" + builder_.config().compiler);
                if (builder_.config().compiler == "gcc" || builder_.config().compiler == "clang")
                    configure.push_back("-DCMAKE_C_COMPILER=" + builder_.config().compiler);
            }
            std::vector<std::string> build = {"cmake", "--build", build_dir};
            std::cout << "\nConfigure: " << pgr_process::display(configure) << '\n';
            std::cout << "Build: " << pgr_process::display(build) << '\n';
            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                set_status("Отменено");
                return;
            }
            log_.write("[cmake] configure: " + pgr_process::display(configure));
            const int rc1 = pgr_process::run(configure, false, [this](const std::string& line) {
                log_.write("[cmake configure] " + line);
            });
            log_.write("[cmake] configure rc=" + std::to_string(rc1));
            if (rc1 != 0) {
                set_status("Configure failed, rc=" + std::to_string(rc1), true);
                wait_enter();
                return;
            }
            log_.write("[cmake] build: " + pgr_process::display(build));
            const int rc2 = pgr_process::run(build, false, [this](const std::string& line) {
                log_.write("[cmake build] " + line);
            });
            log_.write("[cmake] build rc=" + std::to_string(rc2));
            set_status(rc2 == 0 ? "Сборка CMake: успех" :
                       "Build failed, rc=" + std::to_string(rc2), rc2 != 0);
            wait_enter();
        }

        // ------------------------------------------------------------------
        // Действия: конфиг (20-21)
        // ------------------------------------------------------------------

        void do_save_config() {
            int st = builder_.save(kConfigPath);
            if (st == 0x00) {
                log_.write("[config] saved to " + std::string(kConfigPath));
                set_status("Сохранено в " + std::string(kConfigPath));
            } else {
                log_.error("save_config rc=" + std::to_string(st));
                set_status("Ошибка сохранения, rc=" + std::to_string(st), true);
            }
        }

        void do_load_config() {
            int st = builder_.load(kConfigPath);
            if (st == 0x00) {
                log_.write("[config] loaded from " + std::string(kConfigPath));
                set_status("Загружено из " + std::string(kConfigPath));
            } else if (st == 0x04) {
                log_.write("[config] load: file not found");
                set_status("Файл " + std::string(kConfigPath) + " не найден", true);
            } else {
                log_.error("load_config rc=" + std::to_string(st));
                set_status("Ошибка загрузки, rc=" + std::to_string(st), true);
            }
        }

        // ------------------------------------------------------------------
        // Действия: пакеты, профили, сервис (22-25)
        // ------------------------------------------------------------------

        void do_packages() {
            auto cur = builder_.config().packages;
            edit_string_list("Пакеты (find_package)",
                             cur,
                             "Имя (fmt) или name|target::name: ",
                             false, false);
            builder_.set_packages(cur);
            log_.write("[set] packages=" + disp_list(cur));
            set_status("Пакеты: " + disp_list(cur));
        }

        void do_profile() {
            namespace fs = std::filesystem;

            std::vector<std::string> profiles;
            if (fs::is_directory(kProfilesDir)) {
                for (const auto& e : fs::directory_iterator(kProfilesDir)) {
                    if (e.is_regular_file() && e.path().extension() == ".cfg") {
                        profiles.push_back(e.path().stem().string());
                    }
                }
            }
            std::sort(profiles.begin(), profiles.end());

            if (profiles.empty()) {
                std::cout << "\nПрофилей нет. Создайте файлы в "
                          << kProfilesDir << "/NAME.cfg\n";
                set_status("Нет профилей в " + std::string(kProfilesDir), true);
                wait_enter();
                return;
            }

            std::cout << "\nПрофили (0 = custom, без профиля):\n";
            std::cout << " 0. (custom / config.cfg)\n";
            for (std::size_t i = 0; i < profiles.size(); ++i) {
                std::cout << " " << (i + 1) << ". " << profiles[i];
                if (profiles[i] == builder_.config().profile)
                    std::cout << "  <- текущий";
                std::cout << '\n';
            }

            int choice = -1;
            if (ask_int("Выбор: ", 0, static_cast<int>(profiles.size()), choice)
                != input_result::ok) return;

            if (choice == 0) {
                if (builder_.load(kConfigPath) != 0) {
                    set_status("Не удалось загрузить custom-конфигурацию", true);
                    return;
                }
                builder_.set_profile("");
                log_.write("[profile] restored custom config");
                set_status("Профиль: custom");
                return;
            }

            const std::string name = profiles[choice - 1];
            const std::string path = std::string(kProfilesDir) + "/" + name + ".cfg";
            int st = builder_.load(path);
            if (st == 0x00) {
                builder_.set_profile(name);
                log_.write("[profile] loaded " + name);
                set_status("Профиль: " + name);
            } else {
                log_.error("profile load failed: " + name);
                set_status("Ошибка загрузки профиля " + name, true);
            }
        }

        void do_clean_cmake() {
            namespace fs = std::filesystem;
            if (!fs::exists("build_cmake")) {
                set_status("build_cmake не существует", true);
                return;
            }
            bool ok = false;
            if (ask_yesno("Удалить build_cmake полностью?", ok)
                != input_result::ok || !ok) {
                set_status("Отменено");
                return;
            }
            std::string error;
            if (!pgr_paths::clean("build_cmake", error)) {
                log_.error("clean_cmake: " + error);
                set_status("Ошибка удаления: " + error, true);
            } else {
                log_.write("[cmake] clean build_cmake");
                set_status("build_cmake удалён");
            }
        }

        void do_copy_compile_commands() {
            namespace fs = std::filesystem;
            const fs::path src = "build_cmake/compile_commands.json";
            const fs::path dst = "compile_commands.json";

            if (!fs::exists(src)) {
                log_.error("compile_commands.json not found");
                set_status("Нет " + src.string() + " (сначала п.19)", true);
                return;
            }
            std::error_code ec;
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                log_.error("copy cc.json: " + ec.message());
                set_status("Ошибка: " + ec.message(), true);
            } else {
                log_.write("[cmake] copied compile_commands.json");
                set_status("compile_commands.json скопирован");
            }
        }

        void do_advanced(int item) {
            const auto& cfg = builder_.config();
            std::string previous = item == 26 ? cfg.toolchain_file :
                item == 27 ? cfg.c_standard : item == 28 ? cfg.linker_script : cfg.target_type;
            std::string value;
            const std::string prompt = item == 26 ? "Путь к toolchain.cmake: " :
                item == 27 ? "Стандарт C (-std=c11): " :
                item == 28 ? "Путь к linker.ld: " : "Библиотеки (static/shared): ";
            if (ask_line(prompt, value, true) != input_result::ok) return;
            auto assign = [&](const std::string& v) {
                switch (item) {
                    case 26: builder_.set_toolchain_file(v); break;
                    case 27: builder_.set_c_standard(v); break;
                    case 28: builder_.set_linker_script(v); break;
                    case 29: builder_.set_target_type(v); break;
                }
            };
            assign(value);
            if (builder_.validate_configuration() != 0) {
                assign(previous);
                set_status("Некорректное значение параметра", true);
            } else {
                log_.write("[config] advanced setting " + std::to_string(item) + ": " + value);
                set_status("Параметр обновлён");
            }
        }

        // ------------------------------------------------------------------
        // Диспетчер
        // ------------------------------------------------------------------

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
                case 22: return "packages";
                case 23: return "profile";
                case 24: return "clean_cmake";
                case 25: return "copy_compile_commands";
                case 0:  return "exit";
                default: return "unknown";
            }
        }

        void handle(int choice) {
            log_.write("[menu] " + std::to_string(choice) +
                       " (" + choice_name(choice) + ")");

            switch (choice) {
                case 1:  do_compiler();              break;
                case 2:  do_standard();              break;
                case 3:  do_optimization();          break;
                case 4:  do_debug();                 break;
                case 5:  do_warnings();              break;
                case 6:  do_sanitizers();            break;
                case 7:  do_lto();                   break;
                case 8:  do_arch();                  break;
                case 9:  do_specific_gcc();          break;
                case 10: do_specific_clang();        break;
                case 11: do_source_files();          break;
                case 12: do_output_file();           break;
                case 13: do_include_dirs();          break;
                case 14: do_library_dirs();          break;
                case 15: do_libraries();             break;
                case 16: do_show_command();          break;
                case 17: do_build();                 break;
                case 18: do_generate_cmake();        break;
                case 19: do_build_cmake();           break;
                case 20: do_save_config();           break;
                case 21: do_load_config();           break;
                case 22: do_packages();              break;
                case 23: do_profile();               break;
                case 24: do_clean_cmake();           break;
                case 25: do_copy_compile_commands(); break;
                case 26: case 27: case 28: case 29: do_advanced(choice); break;
                case 0:  running_ = false;           break;
                default: set_status("Нет такого пункта", true); break;
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
                std::cerr << "Не удалось открыть " << kLogPath
                          << " — работаю без логирования\n";
            }
            log_.write("=== Session started ===");

            if (catalog_.load("config") != 0x00) {
                std::cerr << "Не удалось загрузить config/\n";
                return;
            }
            log_.write("[catalog] loaded");

            if (builder_.load(kConfigPath) != 0x00) {
                log_.write("[config] not found, applying defaults");
                builder_.set_compiler("g++");
                builder_.set_standard("-std=c++17");
                builder_.set_optimization("-Os");
                builder_.set_warnings({"-Wall", "-Wextra"});
                builder_.set_specific_gcc({"-fno-declone-ctor-dtor"});
                builder_.set_source_files({"main.cpp"});
                builder_.save(kConfigPath);
                set_status("Применены значения по умолчанию");
            } else {
                log_.write("[config] loaded from " + std::string(kConfigPath));
                set_status("Загружено из " + std::string(kConfigPath));
            }

            while (running_) {
                print_menu();

                int choice = -1;
                auto r = ask_int("Выбор: ", 0, 29, choice);
                if (r == input_result::eof) {
                    log_.write("[menu] EOF");
                    break;
                }
                if (r == input_result::cancelled) continue;

                const auto before = builder_.config();
                handle(choice);
                if (builder_.config().profile.empty() && builder_.config() != before)
                    builder_.save(kConfigPath);
            }

            log_.write("=== Session ended ===");

            clear_screen();
            std::cout << "Настройки: " << kConfigPath << '\n'
                      << "Профили:   " << kProfilesDir << "/*.cfg\n"
                      << "Лог:       " << kLogPath << '\n'
                      << "До встречи!\n";
        }
};

#endif // TUI_HPP