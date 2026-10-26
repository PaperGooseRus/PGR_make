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
 * Один экземпляр может поочерёдно писать в разные файлы и в std::cout.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 вывод не готов (файл не открыт)
 *  - @c 0x02 ошибка записи
 *  - @c 0x03 файл не удалось открыть (возвращает @ref open())
 */
class write_t {
    private:
        std::string path_;
        std::ofstream file_;
        std::ostream* out_ = &std::cout;
        int open_status_ = 0x00;

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
        write_t() = default;

        write_t(const std::string& path,
                std::ios::openmode mode = std::ios::out)
        {
            open(path, mode);
        }

        write_t(const write_t&) = delete;
        write_t& operator=(const write_t&) = delete;

        ~write_t() = default;

        /**
         * @brief Переключиться на запись в файл.
         * @return @c 0x00 при успехе, @c 0x03 если файл не открылся.
         */
        int open(const std::string& path,
                 std::ios::openmode mode = std::ios::out)
        {
            if (path.empty()) {
                std::cerr << "write_t: empty path\n";
                open_status_ = 0x03;
                out_ = nullptr;
                return 0x03;
            }

            if (file_.is_open()) {
                file_.close();
            }

            file_.clear();
            out_ = nullptr;
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

        /// @brief Переключиться на запись в std::cout.
        void open_terminal() {
            if (file_.is_open()) {
                file_.close();
            }
            path_.clear();
            out_ = &std::cout;
            open_status_ = 0x00;
        }

        int status() const { return open_status_; }
        bool is_ready() const { return out_ != nullptr; }
        bool is_terminal() const { return out_ == &std::cout; }

        /// @brief Записать вектор строк (по одной на строку).
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

        /// @brief Записать одну строку.
        int write(const std::string& line) {
            return write_line(line);
        }

        /// @brief Явно сбросить буфер.
        void flush() {
            if (out_ != nullptr) {
                out_->flush();
            }
        }
};

#endif // WRITE_HPP