#ifndef READ_HPP
#define READ_HPP

#include <iostream>
#include <string>
#include <vector>

class read_t {
    private:
        std::string line_;
        std::vector<std::string> arr_;

        int read_line(void) {
            if (!std::getline(std::cin, line_)) {
                if (std::cin.eof()) {
                    return 0x01; // конец ввода
                }

                std::cerr << "Error read user cin line\n";
                return 0x03; // ошибка чтения
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

        int read(void) {
            int read_status = 0x00;

            while (read_status == 0x00) {
                read_status = read_line();
            }

            return read_status;
        }

        const std::vector<std::string>& data(void) const {
            for (std::size_t index = 0; index < arr_.size(); ++index) {
                std::cout << arr_[index] << std::endl;
            }

            return arr_;
        }
};

#endif // READ_HPP