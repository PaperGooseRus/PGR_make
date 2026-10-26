#ifndef TUI_HPP
#define TUI_HPP

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "make.hpp"
#include "read.hpp"
#include "write.hpp"

/**
 * @file TUI.hpp
 * @brief Интерактивная настройка с проверками и пояснениями флагов.
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
                        std::cout << "  Введите число от " << min << " до " << max << '\n';
                        continue;
                    }
                    out = v;
                    return input_result::ok;
                } catch (...) {
                    std::cout << "  Введите число от " << min << " до " << max << '\n';
                }
            }
        }

        static input_result ask_yesno(const std::string& prompt, bool& out) {
            while (true) {
                std::string s;
                auto r = ask_line(prompt + " [y/n]: ", s);
                if (r != input_result::ok) return r;
                if (s == "y" || s == "Y" || s == "yes" || s == "да") { out = true; return input_result::ok; }
                if (s == "n" || s == "N" || s == "no" || s == "нет") { out = false; return input_result::ok; }
                std::cout << "  Введите y или n\n";
            }
        }

        // ---------- Отображение ----------
        static std::string disp(const std::string& v) { return v.empty() ? "(не задано)" : v; }
        static std::string disp_list(const std::vector<std::string>& v) {
            if (v.empty()) return "(не задано)";
            std::string s;
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) s += ", ";
                s += v[i];
            }
            return s;
        }

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
                      << "Исходник:        " << disp(c.source_file)   << '\n'
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
                      << "11. Исходный файл\n"
                      << "12. Выходной файл\n"
                      << "13. Показать команду\n"
                      << "14. Собрать\n"
                      << " 0. Выход\n";
        }

        // ---------- Выбор с пояснениями ----------
        int select_single_from(const flag_group_t& g, const std::string& current) {
            if (g.entries.empty()) {
                std::cout << "  Нет опций (проверьте config/" << g.name << ".txt)\n";
                return -1;
            }

            std::cout << " 0. (не задано / оставить текущее)\n";
            for (std::size_t i = 0; i < g.entries.size(); ++i) {
                std::cout << " " << (i + 1) << ". " << g.entries[i].flag;
                if (!g.entries[i].description.empty()) {
                    std::cout << "  -- " << g.entries[i].description;
                }
                if (g.entries[i].flag == current) std::cout << "  <- текущее";
                std::cout << '\n';
            }

            int choice = -1;
            auto r = ask_int("Выбор: ", 0, static_cast<int>(g.entries.size()), choice);
            if (r != input_result::ok) return -1;
            return choice;
        }

        input_result select_multi_from(const flag_group_t& g, std::vector<std::string>& current) {
            if (g.entries.empty()) {
                std::cout << "  Нет опций (проверьте config/" << g.name << ".txt)\n";
                return input_result::cancelled;
            }

            while (true) {
                std::cout << "\n" << g.title << " (0 - готово, пусто - отмена):\n";
                for (std::size_t i = 0; i < g.entries.size(); ++i) {
                    bool on = std::find(current.begin(), current.end(), g.entries[i].flag) != current.end();
                    std::cout << " [" << (on ? "x" : " ") << "] " << (i + 1) << ". " << g.entries[i].flag;
                    if (!g.entries[i].description.empty()) {
                        std::cout << "  -- " << g.entries[i].description;
                    }
                    std::cout << '\n';
                }

                int choice = -1;
                auto r = ask_int("Toggle: ", 0, static_cast<int>(g.entries.size()), choice);
                if (r != input_result::ok) return r;
                if (choice == 0) return input_result::ok;

                const std::string& flag = g.entries[choice - 1].flag;
                auto it = std::find(current.begin(), current.end(), flag);
                if (it != current.end()) current.erase(it);
                else                     current.push_back(flag);
            }
        }

        // ---------- Действия ----------
        void do_compiler() {
            auto* g = catalog_.find("compiler");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().compiler);
            if (choice < 0) return;
            std::string val = (choice == 0) ? "g++" : g->entries[choice - 1].flag;

            // Проверка наличия в PATH (эвристика через system)
            std::string check_cmd = "where " + val + " >nul 2>nul";
#ifdef __linux__
            check_cmd = "command -v " + val + " >/dev/null 2>&1";
#endif
            if (std::system(check_cmd.c_str()) != 0) {
                std::cout << "  ВНИМАНИЕ: '" << val << "' не найден в PATH\n";
                bool cont = false;
                if (ask_yesno("Всё равно использовать?", cont) != input_result::ok || !cont) {
                    std::cout << "  Отменено\n";
                    return;
                }
            }

            builder_.set_compiler(val);
        }

        void do_standard() {
            auto* g = catalog_.find("standard");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().standard);
            if (choice < 0) return;
            builder_.set_standard(choice == 0 ? "" : g->entries[choice - 1].flag);
        }

        void do_optimization() {
            auto* g = catalog_.find("optimization");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().optimization);
            if (choice < 0) return;
            builder_.set_optimization(choice == 0 ? "" : g->entries[choice - 1].flag);
        }

        void do_debug() {
            auto* g = catalog_.find("debug");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().debug);
            if (choice < 0) return;
            builder_.set_debug(choice == 0 ? "" : g->entries[choice - 1].flag);
        }

        void do_lto() {
            auto* g = catalog_.find("lto");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().lto);
            if (choice < 0) return;
            builder_.set_lto(choice == 0 ? "" : g->entries[choice - 1].flag);
        }

        void do_arch() {
            auto* g = catalog_.find("arch");
            if (!g) return;
            int choice = select_single_from(*g, builder_.config().arch);
            if (choice < 0) return;
            builder_.set_arch(choice == 0 ? "" : g->entries[choice - 1].flag);
        }

        void do_warnings() {
            auto* g = catalog_.find("warnings");
            if (!g) return;
            std::vector<std::string> current = builder_.config().warnings;
            if (select_multi_from(*g, current) == input_result::ok) {
                builder_.set_warnings(current);
            }
        }

        void do_sanitizers() {
            auto* g = catalog_.find("sanitizers");
            if (!g) return;
            std::vector<std::string> current = builder_.config().sanitizers;
            if (select_multi_from(*g, current) == input_result::ok) {
                // Проверка на несовместимость после выбора
                auto has = [&current](const std::string& s) {
                    return std::find(current.begin(), current.end(), s) != current.end();
                };
                if (has("-fsanitize=address") && has("-fsanitize=thread")) {
                    std::cout << "  ОШИБКА: address и thread несовместимы. Изменения отменены.\n";
                    return;
                }
                if (has("-fsanitize=address") && has("-fsanitize=memory")) {
                    std::cout << "  ОШИБКА: address и memory несовместимы. Изменения отменены.\n";
                    return;
                }
                if (has("-fsanitize=thread") && has("-fsanitize=leak")) {
                    std::cout << "  ОШИБКА: thread и leak несовместимы. Изменения отменены.\n";
                    return;
                }
                builder_.set_sanitizers(current);
            }
        }

        void do_specific_gcc() {
            auto* g = catalog_.find("specific_gcc");
            if (!g) return;
            std::vector<std::string> current = builder_.config().specific_gcc;
            if (select_multi_from(*g, current) == input_result::ok) {
                builder_.set_specific_gcc(current);
            }
        }

        void do_specific_clang() {
            auto* g = catalog_.find("specific_clang");
            if (!g) return;
            std::vector<std::string> current = builder_.config().specific_clang;
            if (select_multi_from(*g, current) == input_result::ok) {
                builder_.set_specific_clang(current);
            }
        }

        void do_source_file() {
            std::cout << "Текущий: " << disp(builder_.config().source_file) << '\n';
            std::string v;
            if (ask_line("Новый (пусто - оставить): ", v, true) != input_result::ok) return;
            if (v.empty()) return;
            if (v.find_first_of(";|&`$<>\"") != std::string::npos) {
                std::cout << "  Недопустимые символы\n";
                return;
            }
            if (!std::filesystem::exists(v)) {
                std::cout << "  ВНИМАНИЕ: файл не найден\n";
                bool cont = false;
                if (ask_yesno("Всё равно использовать?", cont) != input_result::ok || !cont) {
                    std::cout << "  Отменено\n";
                    return;
                }
            }
            builder_.set_source_file(v);
        }

        void do_output_file() {
            std::cout << "Текущий: " << disp(builder_.config().output_file) << '\n';
            std::string v;
            if (ask_line("Новый (пусто - оставить): ", v, true) != input_result::ok) return;
            if (v.empty()) return;
            if (v.find_first_of(";|&`$<>\"") != std::string::npos) {
                std::cout << "  Недопустимые символы\n";
                return;
            }
            builder_.set_output_file(v);
        }

        void do_show_command() {
            if (builder_.config().source_file.empty()) {
                std::cout << "Сначала задайте исходный файл (п. 11)\n";
                return;
            }
            if (builder_.build_args() != 0x00) {
                std::cout << "Ошибка конфигурации (подробности выше)\n";
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
            if (builder_.config().source_file.empty()) {
                std::cout << "Сначала задайте исходный файл (п. 11)\n";
                return;
            }

            if (builder_.build_args() != 0x00) {
                std::cout << "Ошибка конфигурации (подробности выше)\n";
                return;
            }
            builder_.build_command(builder_.args());

            // Проверка существования исходника
            if (!std::filesystem::exists(builder_.config().source_file)) {
                std::cout << "  ВНИМАНИЕ: исходный файл не найден\n";
                bool cont = false;
                if (ask_yesno("Продолжить?", cont) != input_result::ok || !cont) {
                    std::cout << "Отменено\n";
                    return;
                }
            }

            // Создание каталога для выходного файла
            if (!builder_.config().output_file.empty()) {
                auto parent = std::filesystem::path(builder_.config().output_file).parent_path();
                if (!parent.empty() && !std::filesystem::exists(parent)) {
                    std::error_code ec;
                    std::filesystem::create_directories(parent, ec);
                    if (ec) {
                        std::cout << "  ОШИБКА: не удалось создать каталог " << parent
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
                case 11: do_source_file();    break;
                case 12: do_output_file();    break;
                case 13: do_show_command();   break;
                case 14: do_build();          break;
                case 0:  running_ = false;    break;
                default: std::cout << "Нет такого пункта\n"; break;
            }
        }

    public:
        tui_t() = default;
        ~tui_t() = default;

        void run() {
            // Проверка и создание build/ с обработкой ошибок
            std::error_code ec;
            std::filesystem::create_directories("build", ec);
            if (ec) {
                std::cerr << "Не удалось создать build/: " << ec.message() << '\n';
                return;
            }

            // Загрузка каталога
            if (catalog_.load("config") != 0x00) {
                std::cerr << "Не удалось загрузить ни одного файла из config/\n";
                return;
            }

            while (running_) {
                print_menu();

                int choice = -1;
                auto r = ask_int("Выбор: ", 0, 14, choice);
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

#endif // TUI.hpp