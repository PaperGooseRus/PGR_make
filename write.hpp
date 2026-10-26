#ifndef WRITE_HPP
#define WRITE_HPP

#include <iostream>
#include <string>
#include <vector>
#include <fstream>

class write_t {
    private:
        std::string write_path_ = "";
        std::ofstream out_;

        int write_line(const std::string& line) {
            if (!out_) {
                return 0x01; // файл не открыт
            }

            out_ << line << std::endl;

            if (!out_) {
                return 0x02; // ошибка записи (диск заполнен, нет прав и т.п.)
            }

            return 0x00;
        }

    public:
        // mode по умолчанию — ios::out (перезапись файла)
        write_t(const std::string& path, std::ios::openmode mode = std::ios::out): write_path_(path), out_(path, mode) {
            if (!out_) {
                std::cerr << "Cannot open file for write: " << write_path_ << std::endl;
            }
        };
        ~write_t() = default;

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

        int write(const std::string& line) {
            return write_line(line);
        }
};

#endif // WRITE_HPP