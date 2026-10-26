#ifndef WRITE_HPP
#define WRITE_HPP

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

/**
 * @file write.hpp
 * @brief Построчная запись в файл.
 */

/**
 * @class write_t
 * @brief Записывает строки в файл.
 *
 * Файл открывается в конструкторе (RAII) и закрывается автоматически
 * в деструкторе. Режим открытия по умолчанию — @c std::ios::out
 * (перезапись). Для дозаписи используйте @c std::ios::app.
 *
 * Соглашение о кодах возврата (общее для всех методов):
 *  - @c 0x00 успех
 *  - @c 0x01 файл не открыт
 *  - @c 0x02 ошибка записи
 */
class write_t {
    private:
        std::string write_path_;  ///< Путь к выходному файлу.
        std::ofstream out_;       ///< Выходной поток (управляется RAII).

        /**
         * @brief Записать одну строку с завершающим переводом строки.
         * @param line Текст для записи.
         * @return @c 0x00 при успехе, @c 0x01 если файл не открыт,
         *         @c 0x02 при ошибке записи.
         */
        int write_line(const std::string& line) {
            if (!out_) {
                return 0x01; // файл не открыт
            }

            out_ << line << '\n';

            if (!out_) {
                return 0x02; // ошибка записи
            }

            return 0x00;
        }

    public:
        /**
         * @brief Открыть файл для записи.
         * @param path Путь к выходному файлу.
         * @param mode Режим открытия (по умолчанию — @c std::ios::out).
         *
         * Если файл не открылся, в std::cerr выводится сообщение;
         * последующие вызовы записи вернут @c 0x01.
         */
        write_t(const std::string& path,
                std::ios::openmode mode = std::ios::out)
            : write_path_(path), out_(path, mode)
        {
            if (!out_) {
                std::cerr << "write_t: cannot open " << write_path_ << '\n';
            }
        }

        ~write_t() = default;

        /**
         * @brief Записать каждую строку вектора отдельной строкой.
         * @param lines Строки для записи.
         * @return @c 0x00 при успехе, @c 0x01 если файл не открыт,
         *         @c 0x02 при ошибке записи.
         */
        int write(const std::vector<std::string>& lines) {
            if (!out_) {
                return 0x01;
            }

            for (const auto& s : lines) {
                out_ << s << '\n';
            }

            if (!out_) {
                return 0x02;
            }

            return 0x00;
        }

        /**
         * @brief Записать одну строку.
         * @param line Текст для записи.
         * @return То же, что и @ref write_line.
         */
        int write(const std::string& line) {
            return write_line(line);
        }
};

#endif // WRITE_HPP