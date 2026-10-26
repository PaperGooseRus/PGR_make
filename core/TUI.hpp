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
#include "read.hpp"
#include "tree.hpp"
#include "write.hpp"

/**
 * @file TUI.hpp
 * @brief Меню конфигурации, генерации CMakeLists.txt и запуска сборки.
 *
 * UX-улучшения:
 *  - полная перерисовка экрана после каждого действия;
 *  - компактная сводка в шапке (одна строка вместо 15);
 *  - значения флагов встроены в пункты меню;
 *  - статус-строка с результатом последнего действия;
 *  - группы: Настройки / Действия / Выход, разделители.
 */
class tui_t {
    private:
        enum class input_result { ok, cancelled, eof };

        make_t builder_;
        flag_catalog_t catalog_;
        log_t log_;
        bool running_ = true;

        std::string status_;             ///< Текст статуса для следующей отрисовки.
        bool status_is_error_ = false;   ///< Статус — ошибка ([!] вместо [ ]).

        static constexpr const char* kConfigPath = "project/config.cfg";
        static constexpr const char* kLogPath    = "project/work.log";
        static constexpr const char* kTitle      = "PGR_make";

        // ------------------------------------------------------------------
        // Экран
        // ------------------------------------------------------------------

#ifdef _WIN32
        /// @brief Очистка экрана через Win32 (быстро, без мерцания).
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
        /// @brief Очистка экрана через ANSI.
        static void clear_screen() {
            std::cout << "\033[2J\033[H" << std::flush;
        }
#endif

        /// @brief Пауза до Enter (для длинного вывода перед перерисовкой).
        static void wait_enter() {
            std::cout << "\n-- Enter --" << std::flush;
            std::string line;
            std::getline(std::cin, line);
        }

        /// @brief Ширина строки в печатных столбцах (UTF-8 aware).
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

        /// @brief Дополнить строку пробелами справа до display_width = width.
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
        // Отображение значений
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

        // ------------------------------------------------------------------
        // Шапка и меню
        // ------------------------------------------------------------------

        void print_summary_line() const {
            const auto& c = builder_.config();

            print_sep('=', 74);
            std::cout << " " << kTitle << '\n';
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

            const std::size_t extra =
                c.warnings.size() + c.sanitizers.size()
              + c.specific_gcc.size() + c.specific_clang.size()
              + c.include_dirs.size() + c.library_dirs.size()
              + c.libraries.size();
            if (extra > 0) append("opt:" + std::to_string(extra));

            if (!c.source_files.empty())
                append("src:" + std::to_string(c.source_files.size()));

            std::cout << " " << (s.empty() ? "(по умолчанию)" : s) << '\n';
            print_sep('-', 74);
        }

        void print_status_line() {
            if (status_.empty()) return;
            std::cout << " Статус: " << (status_is_error_ ? "[!] " : "") << status_ << '\n';
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
        // Редактор списка (sub-меню, без перерисовки — чтобы видеть изменения)
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
        // Выбор из каталога
        // ------------------------------------------------------------------

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

        // ------------------------------------------------------------------
        // Действия: single-группы
        // ------------------------------------------------------------------

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
        // Действия: multi-группы
        // ------------------------------------------------------------------

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
        // Действия: пути и библиотеки
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
        // Действия: команда и сборка (с wait_enter)
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
            int rc = builder_.run();
            log_.write("[gpp] rc=" + std::to_string(rc));

            if (rc == 0x00) set_status("Сборка (g++): успех");
            else            set_status("Сборка (g++): ошибка, rc=" + std::to_string(rc), true);
            wait_enter();
        }

        // ------------------------------------------------------------------
        // Действия: CMake
        // ------------------------------------------------------------------

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
                log_.error("tree.scan failed");
                set_status("Ошибка сканирования", true);
                return;
            }

            const auto cyc = tree.cycles();
            if (!cyc.empty()) {
                std::string cyc_str;
                std::cout << "ОБНАРУЖЕНЫ ЦИКЛЫ:\n";
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
                set_status("Обнаружены циклы — CMake невозможен", true);
                wait_enter();
                return;
            }

            cmake_layout_t layout;
            if (layout.build(tree) != 0x00) {
                log_.error("cmake_layout.build failed");
                set_status("Не удалось построить план", true);
                return;
            }

            std::cout << "\n=== Таргеты ===\n";
            tree.print_targets();
            std::cout << "\n=== План CMake ===\n";
            layout.print();

            bool ok = false;
            if (std::filesystem::exists("CMakeLists.txt")) {
                if (ask_yesno("\nCMakeLists.txt существует. Перезаписать?", ok)
                    != input_result::ok || !ok) {
                    set_status("Отменено");
                    return;
                }
            } else {
                if (ask_yesno("\nЗаписать CMakeLists.txt на диск?", ok)
                    != input_result::ok || !ok) {
                    set_status("Отменено");
                    return;
                }
            }

            int st = cmake_writer_t::save(tree, layout, builder_.config());
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
            if (!std::filesystem::exists("CMakeLists.txt")) {
                log_.error("build_cmake: CMakeLists.txt missing");
                set_status("CMakeLists.txt не найден (п. 18)", true);
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
                if (ask_yesno("Использовать Ninja?", use_ninja)
                    == input_result::ok && use_ninja) {
                    generator_opt = " -G Ninja";
                }
            }

            std::string configure_cmd =
                "cmake -S . -B " + build_dir + generator_opt
                + " -DCMAKE_CXX_COMPILER=" + builder_.config().compiler;
            std::string build_cmd = "cmake --build " + build_dir;

            std::cout << "\nConfigure: " << configure_cmd << '\n';
            std::cout << "Build:     " << build_cmd << '\n';

            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                set_status("Отменено");
                return;
            }

            std::cout << "\n--- cmake configure ---\n";
            log_.write("[cmake] configure: " + configure_cmd);
            int rc1 = std::system(configure_cmd.c_str());
            log_.write("[cmake] configure rc=" + std::to_string(rc1));
            if (rc1 != 0) {
                set_status("Configure failed, rc=" + std::to_string(rc1), true);
                wait_enter();
                return;
            }

            std::cout << "\n--- cmake build ---\n";
            log_.write("[cmake] build: " + build_cmd);
            int rc2 = std::system(build_cmd.c_str());
            log_.write("[cmake] build rc=" + std::to_string(rc2));
            if (rc2 != 0) {
                set_status("Build failed, rc=" + std::to_string(rc2), true);
            } else {
                set_status("Сборка CMake: успех (" + build_dir + ")");
            }
            wait_enter();
        }

        // ------------------------------------------------------------------
        // Действия: конфиг
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
                case 0:  return "exit";
                default: return "unknown";
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
                auto r = ask_int("Выбор: ", 0, 21, choice);
                if (r == input_result::eof) {
                    log_.write("[menu] EOF");
                    break;
                }
                if (r == input_result::cancelled) continue;

                handle(choice);
                builder_.save(kConfigPath);
            }

            builder_.save(kConfigPath);
            log_.write("=== Session ended ===");

            clear_screen();
            std::cout << "Настройки: " << kConfigPath << '\n'
                      << "Лог:       " << kLogPath << '\n'
                      << "До встречи!\n";
        }
};

#endif // TUI_HPP