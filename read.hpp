#ifndef READ_HPP
#define READ_HPP

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

/**
 * @file read.hpp
 * @brief Построчное чтение из терминала, файла или произвольного потока.
 */

/**
 * @class read_t
 * @brief Накапливает строки из std::cin, файла или произвольного потока.
 *
 * Один экземпляр может читать из разных источников: перед новым чтением
 * вызовите @ref clear(), чтобы не смешивать данные.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 конец ввода (EOF)
 *  - @c 0x02 пустая строка — маркер конца
 *  - @c 0x03 ошибка чтения
 *  - @c 0x04 ошибка открытия файла
 */
class read_t {
    private:
        std::vector<std::string> arr_;   ///< Накопленные непустые строки.

        /**
         * @brief Читать строки из потока до EOF или пустой строки.
         * @param in Входной поток.
         * @return @c 0x01 при EOF, @c 0x02 при пустой строке,
         *         @c 0x03 при ошибке чтения.
         */
        int read_all(std::istream& in) {
            std::string line;

            while (true) {
                if (!std::getline(in, line)) {
                    if (in.eof()) {
                        return 0x01;
                    }

                    std::cerr << "read_t: read error\n";
                    return 0x03;
                }

                if (line.empty()) {
                    return 0x02;
                }

                arr_.push_back(line);
            }
        }

    public:
        read_t() = default;
        ~read_t() = default;

        /**
         * @brief Читать из std::cin до EOF или пустой строки.
         * @return Код возврата (см. описание класса).
         */
        int read() {
            return read_all(std::cin);
        }

        /**
         * @brief Читать из произвольного потока до EOF или пустой строки.
         *
         * Удобно для тестов через std::istringstream и для собственных
         * std::ifstream.
         *
         * @param in Входной поток.
         * @return Код возврата (см. описание класса).
         */
        int read(std::istream& in) {
            return read_all(in);
        }

        /**
         * @brief Читать из файла до EOF или пустой строки.
         * @param path Путь к входному файлу.
         * @return @c 0x04, если файл не открылся; иначе коды как у @ref read().
         */
        int read(const std::string& path) {
            if (path.empty()) {
                std::cerr << "read_t: empty path\n";
                return 0x04;
            }

            std::ifstream in(path);
            if (!in) {
                std::cerr << "read_t: cannot open " << path << '\n';
                return 0x04;
            }

            return read_all(in);
        }

        /**
         * @brief Доступ к накопленным строкам.
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