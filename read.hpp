#ifndef READ_HPP
#define READ_HPP

#include <iostream>
#include <string>
#include <vector>
#include <fstream>

class read_t {
    private:
        std::string line_;
        std::vector<std::string> arr_;

        int read_line(std::istream& in) {
            if (!std::getline(in, line_)) {
                if (in.eof()) {
                    return 0x01; // конец ввода
                }

                std::cerr << "Error read line\n";

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

        int read_terminal(void) {
            int read_status = 0x00;

            while (read_status == 0x00) {
                read_status = read_line(std::cin);
            }

            return read_status;
        }

        int read_file(const std::string& path) {
            std::ifstream in(path);

            if (!in) {
                std::cerr << "Cannot open file: " << path << std::endl;

                return 0x04;
            }

            int read_status = 0x00;

            while (read_status == 0x00) {
                read_status = read_line(in);
            }

            // ifstream закроется сам в деструкторе
            return read_status;
        }

        const std::vector<std::string>& data() const {
            return arr_;
        }

        void clear() {
            arr_.clear();
        }
};

#endif // READ_HPP