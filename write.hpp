#ifndef WRITE_HPP
#define WRITE_HPP

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

/**
 * @file write.hpp
 * @brief Построчная запись в файл или в терминал.
 */

/**
 * @class write_t
 * @brief Записывает строки в файл или в терминал.
 *
 * Один экземпляр может писать поочерёдно в разные файлы и в std::cout:
 * @code
 * write_t w;                        // по умолчанию — std::cout
 * w.write("hello");                 // в терминал
 * w.open("a.txt");                  // переключились на файл
 * w.write("line");                  // в a.txt
 * w.open("b.txt");                  // переключились на b.txt (a.txt закрыт)
 * w.write("line");                  // в b.txt
 * w.open_terminal();                // вернулись в std::cout
 * @endcode
 *
 * Файл закрывается при следующем open()/open_terminal() и в деструкторе.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 вывод не готов (файл не открыт)
 *  - @c 0x02 ошибка записи
 *  - @c 0x03 файл не удалось открыть (возвращает @ref open())
 */
class write_t {
    private:
        std::string path_;           ///< Путь к текущему файлу ("" для терминала).
        std::ofstream file_;         ///< Файловый поток (RAII-managed).
        std::ostream* out_ = &std::cout; ///< Активный поток: &std::cout или &file_.
        int open_status_ = 0x00;     ///< 0x00 — готов, 0x03 — файл не открылся.

        /**
         * @brief Записать одну строку с завершающим переводом строки.
         * @param line Текст для записи.
         * @return @c 0x00 при успехе, @c 0x01 если вывод не готов,
         *         @c 0x02 при ошибке записи.
         */
        int write_line(const std::string& line) {
            if (out_ == nullptr) {
                return 0x01;
            }

            *out_ << line << '\n';

            if (!*out_) {
                return 0x02;
            }

            return 0x00;
        }

    public:
        /**
         * @brief Конструктор по умолчанию — писать в std::cout.
         */
        write_t() = default;

        /**
         * @brief Конструктор, сразу открывающий файл (для удобства).
         * @param path Путь к выходному файлу.
         * @param mode Режим открытия (по умолчанию — @c std::ios::out).
         *
         * Эквивалент `write_t w; w.open(path, mode);`.
         */
        write_t(const std::string& path, std::ios::openmode mode = std::ios::out) {
            open(path, mode);
        }

        // Копирование запрещено: внутри хранится указатель на собственный поток.
        write_t(const write_t&) = delete;
        write_t& operator=(const write_t&) = delete;

        ~write_t() = default;

        /**
         * @brief Переключиться на запись в файл.
         *
         * Закрывает ранее открытый файл (если был). Если новый файл
         * не открылся, объект остаётся неготовым (запись вернёт @c 0x01),
         * пока не будет вызван успешный @ref open() или @ref open_terminal().
         *
         * @param path Путь к выходному файлу.
         * @param mode Режим открытия (по умолчанию — @c std::ios::out).
         * @return @c 0x00 при успехе, @c 0x03 если файл не открылся.
         */
        int open(const std::string& path, std::ios::openmode mode = std::ios::out) {
            if (file_.is_open()) {
                file_.close();
            }

            file_.clear();   // сбросить failbit от предыдущих операций
            out_ = nullptr;  // до успешного открытия писать нельзя
            path_ = path;

            file_.open(path, mode);

            if (!file_) {
                std::cerr << "write_t: cannot open " << path_ << '\n';
                open_status_ = 0x03;
                return 0x03;
            }

            out_ = &file_;
            open_status_ = 0x00;
            return 0x00;
        }

        /**
         * @brief Переключиться на запись в std::cout.
         *
         * Закрывает ранее открытый файл (если был).
         */
        void open_terminal() {
            if (file_.is_open()) {
                file_.close();
            }

            path_.clear();
            out_ = &std::cout;
            open_status_ = 0x00;
        }

        /**
         * @brief Статус после последнего @ref open().
         * @return @c 0x00 если готов к записи, @c 0x03 если файл не открылся.
         */
        int status() const {
            return open_status_;
        }

        /**
         * @brief Готов ли объект к записи.
         */
        bool is_ready() const {
            return out_ != nullptr;
        }

        /**
         * @brief Пишет ли объект в терминал (а не в файл).
         */
        bool is_terminal() const {
            return out_ == &std::cout;
        }

        /**
         * @brief Записать каждую строку вектора отдельной строкой.
         * @return @c 0x00 при успехе, @c 0x01 если вывод не готов,
         *         @c 0x02 при ошибке записи.
         */
        int write(const std::vector<std::string>& lines) {
            if (out_ == nullptr) {
                return 0x01;
            }

            for (const auto& s : lines) {
                *out_ << s << '\n';
            }

            if (!*out_) {
                return 0x02;
            }

            return 0x00;
        }

        /**
         * @brief Записать одну строку.
         */
        int write(const std::string& line) {
            return write_line(line);
        }

        /**
         * @brief Явно сбросить буфер в целевой поток.
         */
        void flush() {
            if (out_ != nullptr) {
                out_->flush();
            }
        }
};

#endif // WRITE_HPP