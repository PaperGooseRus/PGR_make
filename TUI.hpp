#ifndef TUI_HPP
#define TUI_HPP

#include <iostream>
#include <string>
#include <vector>

#include "read.hpp"
#include "write.hpp"
#include "make.hpp"

/**
 * @file TUI.hpp
 * @brief Пользовательский текстовый интерфейс для read_t/write_t/make_t.
 */

/**
 * @class tui_t
 * @brief Меню-ориентированный текстовый интерфейс.
 *
 * Владеет экземплярами read_t и make_t; write_t создаётся по требованию
 * (терминал — write_t(), файл — write_t(path)).
 *
 * Запуск: @code tui_t tui; tui.run(); @endcode
 *
 * Завершение: пункт 0 меню или EOF (Ctrl+Z / Ctrl+D) на любом запросе.
 */
class tui_t {
    private:
        read_t reader_;            ///< Накопленные строки.
        make_t builder_;           ///< Конфигурация и команда сборки.
        bool running_ = true;      ///< Флаг главного цикла.

        /**
         * @brief Показать запрос и прочитать строку.
         * @param q Текст запроса (без перевода строки).
         * @param out Прочитанная строка.
         * @return @c false при EOF — вызывающий должен завершиться.
         */
        static bool ask(const std::string& q, std::string& out) {
            std::cout << q;
            return static_cast<bool>(std::getline(std::cin, out));
        }

        /**
         * @brief Разобрать целое число из строки целиком.
         * @param s Строка.
         * @param out Результат.
         * @return @c true если вся строка — корректное число.
         */
        static bool parse_int(const std::string& s, int& out) {
            try {
                std::size_t pos = 0;
                int v = std::stoi(s, &pos);
                if (pos != s.size()) {
                    return false;
                }
                out = v;
                return true;
            } catch (...) {
                return false;
            }
        }

        /// @brief Напечатать меню с текущим состоянием.
        void print_menu() const {
            std::cout << "\n=== Меню (строк в памяти: "
                      << reader_.data().size() << ") ===\n"
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
            if (!ask("Путь: ", path)) {
                return;
            }

            reader_.clear();
            int st = reader_.read(path);

            if (st == 0x04) {
                std::cout << "Файл не открылся\n";
                return;
            }

            std::cout << "Статус: " << st
                      << ", строк: " << reader_.data().size() << "\n";
        }

        void do_show_data() {
            if (reader_.data().empty()) {
                std::cout << "(пусто)\n";
                return;
            }

            write_t w; // терминал
            w.write(reader_.data());
        }

        void do_clear() {
            reader_.clear();
            std::cout << "Очищено\n";
        }

        void do_config() {
            std::cout << "Компилятор: 0=gcc, 1=g++, 2=clang\n";

            std::string s;
            if (!ask("Выбор: ", s)) {
                return;
            }

            int c = -1;
            if (!parse_int(s, c) || c < 0 || c > 2) {
                std::cout << "Неверный выбор\n";
                return;
            }

            builder_.set_compiler(static_cast<make_t::compiler>(c));

            std::string v;
            if (ask("Исходный файл: ", v) && !v.empty()) {
                builder_.set_source_file(v);
            }
            if (ask("Выходной файл: ", v) && !v.empty()) {
                builder_.set_output_file(v);
            }
            if (ask("Оптимизация: ", v)) {
                builder_.set_optimization(v);
            }

            std::cout << "Готово\n";
        }

        void do_build_args() {
            builder_.build_args();

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
                std::cout << "(команда не собрана — сначала п. 7)\n";
                return;
            }

            write_t w;
            builder_.write_command(w);
        }

        void do_write_args() {
            std::string path;
            if (!ask("Путь: ", path)) {
                return;
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
            if (!ask("Путь: ", path)) {
                return;
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

            int st = builder_.run();
            std::cout << "Статус: " << st << "\n";
        }

        void do_run_file() {
            std::string path;
            if (!ask("Путь к файлу с командой: ", path)) {
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
                case 0:  running_ = false;   break;
                default: std::cout << "Нет такого пункта\n"; break;
            }
        }

    public:
        tui_t() = default;
        ~tui_t() = default;

        /**
         * @brief Запустить главный цикл меню.
         *
         * Возвращает управление после выбора пункта 0 или при EOF.
         */
        void run() {
            while (running_) {
                print_menu();

                std::string s;
                if (!ask("Выбор: ", s)) {
                    std::cout << "\n(EOF — выход)\n";
                    break;
                }

                int choice = -1;
                if (!parse_int(s, choice)) {
                    std::cout << "Введите число\n";
                    continue;
                }

                handle(choice);
            }

            std::cout << "До встречи!\n";
        }
};

#endif // TUI_HPP