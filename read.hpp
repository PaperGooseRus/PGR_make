#ifndef READ_HPP
#define READ_HPP

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

/**
 * @file read.hpp
 * @brief Построчное чтение из терминала или файла.
 */

/**
 * @class read_t
 * @brief Накапливает строки из терминала (std::cin) или из файла.
 *
 * Читает построчно и складывает непустые строки во внутренний вектор.
 * Чтение прекращается по EOF или при встрече пустой строки.
 *
 * Соглашение о кодах возврата (общее для всех методов):
 *  - @c 0x00 успех
 *  - @c 0x01 конец ввода (EOF)
 *  - @c 0x02 пустая строка — маркер конца
 *  - @c 0x03 ошибка чтения
 *  - @c 0x04 ошибка открытия файла
 */
class read_t {
    private:
        std::string line_;               ///< Буфер текущей строки.
        std::vector<std::string> arr_;   ///< Накопленные непустые строки.

        /**
         * @brief Прочитать одну строку из потока.
         *
         * При успехе строка добавляется в @ref arr_.
         *
         * @param in Входной поток (std::cin или std::ifstream).
         * @return @c 0x00 при успехе, @c 0x01 при EOF, @c 0x02 при
         *         пустой строке, @c 0x03 при ошибке чтения.
         */
        int read_line(std::istream& in) {
            if (!std::getline(in, line_)) {
                if (in.eof()) {
                    return 0x01; // конец ввода
                }

                std::cerr << "read_t: read error\n";

                return 0x03; // реальная ошибка чтения
            }

            if (line_.empty()) {
                return 0x02; // пустая строка как маркер конца
            }

            arr_.push_back(line_);

            return 0x00;
        }

    public:
        read_t() = default;
        ~read_t() = default;

        /**
         * @brief Читать строки из std::cin до EOF или пустой строки.
         *
         * Строки добавляются во внутренний буфер (см. @ref data()).
         * Перед новым чтением при необходимости вызовите @ref clear().
         *
         * @return Итоговый статус: @c 0x01 при EOF, @c 0x02 при пустой
         *         строке, @c 0x03 при ошибке чтения.
         */
        int read_terminal() {
            int read_status = 0x00;

            while (read_status == 0x00) {
                read_status = read_line(std::cin);
            }

            return read_status;
        }

        /**
         * @brief Читать строки из файла до EOF или пустой строки.
         *
         * Строки добавляются во внутренний буфер (см. @ref data()).
         * Перед новым чтением при необходимости вызовите @ref clear().
         *
         * @param path Путь к входному файлу.
         * @return @c 0x04, если файл не открылся; иначе @c 0x01 при EOF,
         *         @c 0x02 при пустой строке, @c 0x03 при ошибке чтения.
         */
        int read_file(const std::string& path) {
            std::ifstream in(path);

            if (!in) {
                std::cerr << "read_t: cannot open " << path << '\n';

                return 0x04;
            }

            int read_status = 0x00;

            while (read_status == 0x00) {
                read_status = read_line(in);
            }

            // ifstream закроется сам в деструкторе
            return read_status;
        }

        /**
         * @brief Доступ к накопленным строкам.
         * @return Константная ссылка на внутренний вектор.
         */
        const std::vector<std::string>& data() const {
            return arr_;
        }

        /**
         * @brief Очистить накопленные строки.
         */
        void clear() {
            arr_.clear();
        }
};

#endif // READ_HPP