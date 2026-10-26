#ifndef TUI_HPP
#define TUI_HPP

#include <filesystem>
#include <iostream>
#include <string>

#include "make.hpp"
#include "read.hpp"
#include "write.hpp"

/**
 * @file TUI.hpp
 * @brief Пользовательский текстовый интерфейс с валидацией ввода.
 */

/**
 * @class tui_t
 * @brief Меню с защитой от типичных ошибок ввода.
 *
 * Принципы:
 *  - Любой запрос можно отменить пустой строкой; EOF (Ctrl+Z / Ctrl+D)
 *    корректно завершает программу.
 *  - Целые числа проверяются на диапазон и на «полное совпадение».
 *  - Свободный ввод заменяется выбором из списка там, где это возможно.
 *  - Опасные действия (run, перезапись файла) требуют подтверждения.
 */
class tui_t {
    private:
        /// @brief Трёхзначный результат запроса.
        enum class input_result { ok, cancelled, eof };

        read_t reader_;
        make_t builder_;
        bool running_ = true;

        static constexpr const char* kConfigPath = "build/config.txt";

        /// @brief Спросить строку. Пустая = отмена (если !allow_empty).
        static input_result ask_line(const std::string& prompt,
                                     std::string& out,
                                     bool allow_empty = false)
        {
            std::cout << prompt;
            if (!std::getline(std::cin, out)) {
                return input_result::eof;
            }
            if (!allow_empty && out.empty()) {
                return input_result::cancelled;
            }
            return input_result::ok;
        }

        /// @brief Спросить целое в [min, max]; повтор при ошибке.
        static input_result ask_int(const std::string& prompt,
                                    int min, int max, int& out)
        {
            while (true) {
                std::string s;
                auto r = ask_line(prompt, s);
                if (r != input_result::ok) {
                    return r;
                }

                try {
                    std::size_t pos = 0;
                    int v = std::stoi(s, &pos);
                    if (pos != s.size()) {
                        std::cout << "  Не число. Введите " << min
                                  << ".." << max << " (пусто — отмена)\n";
                        continue;
                    }
                    if (v < min || v > max) {
                        std::cout << "  Вне диапазона " << min
                                  << ".." << max << "\n";
                        continue;
                    }
                    out = v;
                    return input_result::ok;
                } catch (...) {
                    std::cout << "  Не число. Введите " << min
                              << ".." << max << " (пусто — отмена)\n";
                }
            }
        }

        /// @brief Спросить путь. must_exist = true — проверять существование.
        static input_result ask_path(const std::string& prompt,
                                     std::string& out, bool must_exist)
        {
            while (true) {
                auto r = ask_line(prompt, out);
                if (r != input_result::ok) {
                    return r;
                }

                if (out.find_first_of(";|&`$<>\"\n\r") != std::string::npos) {
                    std::cout << "  Путь содержит недопустимые символы\n";
                    continue;
                }

                if (must_exist && !std::filesystem::exists(out)) {
                    std::cout << "  Файл не найден: " << out << '\n';
                    continue;
                }
                return input_result::ok;
            }
        }

        /// @brief Спросить да/нет.
        static input_result ask_yesno(const std::string& prompt, bool& out) {
            while (true) {
                std::string s;
                auto r = ask_line(prompt + " [y/n]: ", s);
                if (r != input_result::ok) {
                    return r;
                }
                if (s == "y" || s == "Y" || s == "yes" || s == "да") {
                    out = true;
                    return input_result::ok;
                }
                if (s == "n" || s == "N" || s == "no" || s == "нет") {
                    out = false;
                    return input_result::ok;
                }
                std::cout << "  Введите y или n\n";
            }
        }

        void print_menu() const {
            std::cout << "\n=== Меню (строк: " << reader_.data().size() << ") ===\n"
                      << " 1. Читать из терминала\n"
                      << " 2. Читать из файла\n"
                      << " 3. Показать данные\n"
                      << " 4. Очистить данные\n"
                      << " 5. Настроить сборку\n"
                      << " 6. Собрать аргументы\n"
                      << " 7. Собрать команду\n"
                      << " 8. Показать команду\n"
                      << " 9. Записать аргументы в файл\n"
                      << "10. Записать команду в файл\n"
                      << "11. Выполнить команду\n"
                      << "12. Выполнить команду из файла\n"
                      << "13. Сохранить конфиг\n"
                      << "14. Загрузить конфиг\n"
                      << " 0. Выход\n";
        }

        void do_read_terminal() {
            reader_.clear();
            std::cout << "Вводите строки, пустая строка — конец:\n";
            int st = reader_.read();
            std::cin.clear(); // сбросить eof/fail после Ctrl+Z
            std::cout << "Статус: " << st
                      << ", строк: " << reader_.data().size() << "\n";
        }

        void do_read_file() {
            std::string path;
            if (ask_path("Путь: ", path, /*must_exist=*/true) != input_result::ok) {
                return;
            }
            reader_.clear();
            int st = reader_.read(path);
            std::cout << "Статус: " << st
                      << ", строк: " << reader_.data().size() << "\n";
        }

        void do_show_data() {
            if (reader_.data().empty()) {
                std::cout << "(пусто)\n";
                return;
            }
            write_t w;
            w.write(reader_.data());
        }

        void do_clear() {
            reader_.clear();
            std::cout << "Очищено\n";
        }

        void do_config() {
            std::cout << "  Компилятор: 0=gcc, 1=g++, 2=clang\n";
            int c = -1;
            if (ask_int("Компилятор: ", 0, 2, c) != input_result::ok) {
                return;
            }
            builder_.set_compiler(static_cast<make_t::compiler>(c));

            std::string v;
            if (ask_line("Язык (пусто — оставить): ", v, true) == input_result::ok
                && !v.empty()) {
                builder_.set_language(v);
            }

            if (ask_line("Исходный файл (пусто — оставить): ", v, true)
                == input_result::ok && !v.empty()) {
                if (v.find_first_of(";|&`$<>\"") != std::string::npos) {
                    std::cout << "  Недопустимые символы в пути — пропущено\n";
                } else {
                    builder_.set_source_file(v);
                }
            }

            if (ask_line("Выходной файл (пусто — оставить): ", v, true)
                == input_result::ok && !v.empty()) {
                if (v.find_first_of(";|&`$<>\"") != std::string::npos) {
                    std::cout << "  Недопустимые символы в пути — пропущено\n";
                } else {
                    builder_.set_output_file(v);
                }
            }

            std::cout << "  0=-O0 1=-O1 2=-O2 3=-O3 4=-Os 5=(оставить)\n";
            int o = 5;
            if (ask_int("Оптимизация: ", 0, 5, o) == input_result::ok && o != 5) {
                static const char* opts[] = {"-O0", "-O1", "-O2", "-O3", "-Os"};
                builder_.set_optimization(opts[o]);
            }

            std::cout << "Готово\n";
        }

        void do_build_args() {
            int st = builder_.build_args();
            if (st != 0x00) {
                std::cout << "Ошибка конфигурации, статус: " << st << "\n";
                return;
            }
            std::cout << "Аргументы:\n";
            write_t w;
            w.write(builder_.args());
        }

        void do_build_command() {
            builder_.build_command(builder_.args());
            std::cout << "Команда: " << builder_.command() << "\n";
        }

        void do_show_command() {
            if (builder_.command().empty()) {
                std::cout << "(команда не собрана — п. 7)\n";
                return;
            }
            write_t w;
            builder_.write_command(w);
        }

        void do_write_args() {
            std::string path;
            if (ask_path("Путь: ", path, /*must_exist=*/false) != input_result::ok) {
                return;
            }
            if (std::filesystem::exists(path)) {
                bool ok = false;
                if (ask_yesno("Файл существует. Перезаписать?", ok)
                    != input_result::ok || !ok) {
                    std::cout << "Отменено\n";
                    return;
                }
            }
            write_t w(path);
            if (w.status() != 0x00) {
                std::cout << "Файл не открылся\n";
                return;
            }
            int st = builder_.write_args(w);
            std::cout << "Статус: " << st << "\n";
        }

        void do_write_command() {
            std::string path;
            if (ask_path("Путь: ", path, /*must_exist=*/false) != input_result::ok) {
                return;
            }
            if (std::filesystem::exists(path)) {
                bool ok = false;
                if (ask_yesno("Файл существует. Перезаписать?", ok)
                    != input_result::ok || !ok) {
                    std::cout << "Отменено\n";
                    return;
                }
            }
            write_t w(path);
            if (w.status() != 0x00) {
                std::cout << "Файл не открылся\n";
                return;
            }
            int st = builder_.write_command(w);
            std::cout << "Статус: " << st << "\n";
        }

        void do_run() {
            if (builder_.command().empty()) {
                std::cout << "Сначала соберите команду (п. 7)\n";
                return;
            }

            std::cout << "Будет выполнено:\n  " << builder_.command() << '\n';
            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                std::cout << "Отменено\n";
                return;
            }
            int st = builder_.run();
            std::cout << "Статус: " << st << "\n";
        }

        void do_run_file() {
            std::string path;
            if (ask_path("Путь к файлу с командой: ", path, /*must_exist=*/true)
                != input_result::ok) {
                return;
            }

            read_t peek;
            peek.read(path);
            if (!peek.data().empty()) {
                std::cout << "Будет выполнено:\n  " << peek.data()[0] << '\n';
            }
            bool ok = false;
            if (ask_yesno("Выполнить?", ok) != input_result::ok || !ok) {
                std::cout << "Отменено\n";
                return;
            }

            int st = builder_.run_file(path);
            switch (st) {
                case 0x00: std::cout << "OK\n"; break;
                case 0x02: std::cout << "Команда завершилась с ошибкой\n"; break;
                case 0x03: std::cout << "Файл не открылся\n"; break;
                case 0x04: std::cout << "Файл пуст\n"; break;
                case 0x05: std::cout << "Ошибка чтения\n"; break;
                default:   std::cout << "Статус: " << st << "\n"; break;
            }
        }

        void do_save_config() {
            int st = builder_.save(kConfigPath);
            if (st == 0x00) {
                std::cout << "Сохранено в " << kConfigPath << "\n";
            } else {
                std::cout << "Не удалось сохранить, статус: " << st << "\n";
            }
        }

        void do_load_config() {
            make_t::load_report rep;
            int st = builder_.load(kConfigPath, &rep);

            if (st == 0x03) { std::cout << "Файл не открылся\n"; return; }
            if (st == 0x05) { std::cout << "Ошибка чтения\n"; return; }

            for (const auto& e : rep.errors) {
                std::cout << "  ОШИБКА: " << e << '\n';
            }
            for (const auto& w : rep.warnings) {
                std::cout << "  предупреждение: " << w << '\n';
            }

            const auto& c = builder_.config();
            std::cout << "Конфигурация:\n"
                      << "  compiler: "     << static_cast<int>(c.compiler_kind) << '\n'
                      << "  source_file: "  << c.source_file                  << '\n'
                      << "  output_file: "  << c.output_file                  << '\n'
                      << "  optimization: " << c.optimization                << '\n';

            if (rep.ok()) {
                std::cout << "Загружено\n";
            } else {
                std::cout << "Загружено с ошибками (ошибочные поля не изменены)\n";
            }
        }

        void handle(int choice) {
            switch (choice) {
                case 1:  do_read_terminal(); break;
                case 2:  do_read_file();     break;
                case 3:  do_show_data();     break;
                case 4:  do_clear();         break;
                case 5:  do_config();        break;
                case 6:  do_build_args();    break;
                case 7:  do_build_command(); break;
                case 8:  do_show_command();  break;
                case 9:  do_write_args();    break;
                case 10: do_write_command(); break;
                case 11: do_run();           break;
                case 12: do_run_file();      break;
                case 13: do_save_config();   break;
                case 14: do_load_config();   break;
                case 0:  running_ = false;   break;
                default: std::cout << "Нет такого пункта\n"; break;
            }
        }

    public:
        tui_t() = default;
        ~tui_t() = default;

        /// @brief Запустить главный цикл меню.
        void run() {
            builder_.load(kConfigPath); // подхватить прошлый конфиг, если есть

            while (running_) {
                print_menu();

                int choice = -1;
                auto r = ask_int("Выбор: ", 0, 14, choice);
                if (r == input_result::eof) {
                    std::cout << "\n(EOF — выход)\n";
                    break;
                }
                if (r == input_result::cancelled) {
                    continue;
                }
                handle(choice);
            }

            std::cout << "До встречи!\n";
        }
};

#endif // TUI_HPP