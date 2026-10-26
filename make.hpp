#ifndef MAKE_HPP
#define MAKE_HPP

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>

class make_t {
    public:
        enum class compiler {
            GCC   = 0x00,
            GPP   = 0x01,
            CLANG = 0x02
        };

        struct config_make_t {
            std::string language     = "";
            compiler    compiler_    = compiler::GPP;
            std::string target_cpp   = "";
            std::string target_app   = "";
            std::string optimization = "";
        };

    private:
        config_make_t cfg_;
        std::vector<std::string> arr_;
        std::string cmd_;

        static std::string compiler_to_string(compiler c) {
            switch (c) {
                case compiler::GCC:   return "gcc";
                case compiler::GPP:   return "g++";
                case compiler::CLANG: return "clang";
            }
            return "unknown";
        }

    public:
        make_t() = default;
        ~make_t() = default;

        // Сеттеры (нужны, чтобы заполнить конфиг)
        void set_language(const std::string& v)     {
            cfg_.language     = v;
        }

        void set_compiler(compiler c)               {
            cfg_.compiler_    = c;
        }

        void set_target_cpp(const std::string& v)   {
            cfg_.target_cpp   = v;
        }

        void set_target_app(const std::string& v)   {
            cfg_.target_app   = v;
        }

        void set_optimization(const std::string& v) {
            cfg_.optimization = v;
        }

        int make_arr() {
            arr_.clear();

            arr_.push_back(compiler_to_string(cfg_.compiler_));

            if (!cfg_.optimization.empty()) {
                arr_.push_back(cfg_.optimization);
            }

            if (!cfg_.target_cpp.empty()) {
                arr_.push_back(cfg_.target_cpp);
            }

            if (!cfg_.target_app.empty()) {
                arr_.push_back("-o");
                arr_.push_back(cfg_.target_app);
            }

            return 0x00;
        }

        const std::vector<std::string>& data() const {
            return arr_;
        }

        int make(const std::vector<std::string>& v) {
            cmd_.clear();
            const std::string sep = " ";

            std::size_t total = 0;
            for (const auto& s : v) {
                total += s.size();
            }

            if (!v.empty()) {
                total += sep.size() * (v.size() - 1);
            }
            
            cmd_.reserve(total);

            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) {
                    cmd_ += sep;
                }

                cmd_ += v[i];
            }

            return 0x00;
        }

        const std::string& get_cmd() const {
            return cmd_;
        }

        // Выполнить уже собранную команду (cmd_)
        int make_run() {
            if (cmd_.empty()) {
                std::cerr << "make_run: command is empty\n";
                return 0x01;
            }

            std::cout << "Executing: " << cmd_ << '\n';

            int rc = std::system(cmd_.c_str());
            if (rc != 0) {
                std::cerr << "make_run: command failed, rc = " << rc << '\n';
                return 0x02;
            }

            return 0x00;
        }

        // Прочитать команду из файла и выполнить
        int make_run(const std::string& path) {
            std::ifstream in(path);
            if (!in) {
                std::cerr << "make_run: cannot open " << path << '\n';
                return 0x03;
            }

            std::string line;
            if (!std::getline(in, line) || line.empty()) {
                std::cerr << "make_run: empty command in " << path << '\n';
                return 0x04;
            }

            cmd_ = line;   // сохраняем, чтобы был доступен через get_cmd()

            std::cout << "Executing: " << cmd_ << '\n';

            int rc = std::system(cmd_.c_str());
            if (rc != 0) {
                std::cerr << "make_run: command failed, rc = " << rc << '\n';
                return 0x02;
            }

            return 0x00;
        }
};

#endif // MAKE_HPP