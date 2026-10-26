#ifndef MAKE_HPP
#define MAKE_HPP

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "read.hpp"
#include "write.hpp"

/**
 * @file make.hpp
 * @brief Сборка и запуск команды компилятора.
 */

/**
 * @class make_t
 * @brief Формирует команду компилятора из конфигурации и запускает её.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 пустая команда
 *  - @c 0x02 ошибка выполнения команды
 *  - @c 0x03 ошибка открытия файла
 *  - @c 0x04 пустая команда в файле
 *  - @c 0x05 ошибка чтения файла
 *  - @c 0x06 некорректная конфигурация (пустой source_file,
 *    shell-метасимволы в путях)
 */
class make_t {
    public:
        enum class compiler {
            GCC   = 0x00,
            GPP   = 0x01,
            CLANG = 0x02
        };

        struct config_t {
            std::string language      = "";
            compiler    compiler_kind = compiler::GPP;
            std::string source_file   = "";
            std::string output_file   = "";
            std::string optimization  = "";
        };

        /// @brief Результат загрузки конфигурации (ошибки и предупреждения).
        struct load_report {
            std::vector<std::string> errors;
            std::vector<std::string> warnings;

            bool ok() const { return errors.empty(); }
        };

    private:
        config_t cfg_;
        std::vector<std::string> args_;
        std::string command_;

        /// @brief Проверить, содержит ли строка shell-метасимволы.
        static bool has_shell_meta(const std::string& s) {
            return s.find_first_of(";|&`$<>\"\n\r") != std::string::npos;
        }

        /// @brief Заключить строку в кавычки, если в ней есть пробелы.
        static std::string quote_if_needed(const std::string& s) {
            if (s.find_first_of(" \t") == std::string::npos) {
                return s;
            }
            return "\"" + s + "\"";
        }

        /// @brief Имя исполняемого файла компилятора ("g++" и т.п.).
        static std::string compiler_to_command(compiler c) {
            switch (c) {
                case compiler::GCC:   return "gcc";
                case compiler::GPP:   return "g++";
                case compiler::CLANG: return "clang";
            }
            return "unknown";
        }

        /// @brief Имя для сериализации ("GCC" / "GPP" / "CLANG").
        static std::string compiler_to_name(compiler c) {
            switch (c) {
                case compiler::GCC:   return "GCC";
                case compiler::GPP:   return "GPP";
                case compiler::CLANG: return "CLANG";
            }
            return "GPP";
        }

        /// @brief Разбор имени компилятора из файла.
        static compiler name_to_compiler(const std::string& s) {
            if (s == "GCC")   return compiler::GCC;
            if (s == "GPP")   return compiler::GPP;
            if (s == "CLANG") return compiler::CLANG;
            return compiler::GPP; // значение по умолчанию
        }

        static bool valid_optimization(const std::string& v) {
            return v.empty() || v == "-O0" || v == "-O1" ||
                   v == "-O2" || v == "-O3" || v == "-Os";
        }

    public:
        make_t() = default;
        ~make_t() = default;

        /// @name Сеттеры конфигурации
        /// @{
        void set_language(const std::string& v)     { cfg_.language      = v; }
        void set_compiler(compiler c)               { cfg_.compiler_kind = c; }
        void set_source_file(const std::string& v)  { cfg_.source_file   = v; }
        void set_output_file(const std::string& v)  { cfg_.output_file   = v; }
        void set_optimization(const std::string& v) { cfg_.optimization  = v; }
        /// @}

        const config_t& config() const { return cfg_; }

        /**
         * @brief Пересобрать @ref args_ из @ref cfg_.
         *
         * Пути кавычатся при необходимости. Проверяются shell-метасимволы
         * (защита от инъекции в std::system) и пустой source_file.
         *
         * @return @c 0x00 при успехе, @c 0x06 при некорректной конфигурации.
         */
        int build_args() {
            args_.clear();

            if (cfg_.source_file.empty()) {
                std::cerr << "make_t: source_file is empty\n";
                return 0x06;
            }

            if (has_shell_meta(cfg_.source_file) ||
                has_shell_meta(cfg_.output_file)) {
                std::cerr << "make_t: path contains shell metacharacters\n";
                return 0x06;
            }

            args_.push_back(compiler_to_command(cfg_.compiler_kind));

            if (!cfg_.optimization.empty()) {
                args_.push_back(cfg_.optimization);
            }

            args_.push_back(quote_if_needed(cfg_.source_file));

            if (!cfg_.output_file.empty()) {
                args_.push_back("-o");
                args_.push_back(quote_if_needed(cfg_.output_file));
            }

            return 0x00;
        }

        const std::vector<std::string>& args() const { return args_; }

        /**
         * @brief Склеить вектор строк в @ref command_ через пробел.
         */
        int build_command(const std::vector<std::string>& v) {
            command_.clear();
            const std::string sep = " ";

            std::size_t total = 0;
            for (const auto& s : v) {
                total += s.size();
            }
            if (!v.empty()) {
                total += sep.size() * (v.size() - 1);
            }
            command_.reserve(total);

            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) {
                    command_ += sep;
                }
                command_ += v[i];
            }
            return 0x00;
        }

        const std::string& command() const { return command_; }

        int write_args(write_t& out) const {
            return out.write(args_);
        }

        int write_command(write_t& out) const {
            return out.write(command_);
        }

        /**
         * @brief Выполнить @ref command_ через std::system.
         * @return @c 0x00 / @c 0x01 / @c 0x02.
         */
        int run() {
            if (command_.empty()) {
                std::cerr << "make_t: command is empty\n";
                return 0x01;
            }

            std::cout << "Executing: " << command_ << '\n';

            int rc = std::system(command_.c_str());
            if (rc != 0) {
                std::cerr << "make_t: command failed, rc = " << rc << '\n';
                return 0x02;
            }
            return 0x00;
        }

        /**
         * @brief Прочитать команду из файла и выполнить её.
         */
        int run_file(const std::string& path) {
            read_t reader;
            int read_st = reader.read(path);

            if (read_st == 0x04) {
                std::cerr << "make_t: cannot open " << path << '\n';
                return 0x03;
            }
            if (read_st == 0x03) {
                std::cerr << "make_t: read error in " << path << '\n';
                return 0x05;
            }
            if (reader.data().empty()) {
                std::cerr << "make_t: empty command in " << path << '\n';
                return 0x04;
            }

            command_ = reader.data()[0];

            std::cout << "Executing: " << command_ << '\n';

            int rc = std::system(command_.c_str());
            if (rc != 0) {
                std::cerr << "make_t: command failed, rc = " << rc << '\n';
                return 0x02;
            }
            return 0x00;
        }

        // ------------------------------------------------------------------
        // Сериализация конфигурации
        // ------------------------------------------------------------------

        /**
         * @brief Записать конфигурацию в файл в формате key=value.
         *
         * Пустые строки в файл не пишутся: @ref read_t::read() остановится
         * на первой пустой строке. Значения, содержащие @c '\n', не
         * поддерживаются.
         *
         * @param path Путь к файлу конфигурации.
         * @return @c 0x00 / @c 0x03 / @c 0x02.
         */
        int save(const std::string& path) const {
            if (path.empty()) {
                return 0x03;
            }

            std::vector<std::string> lines = {
                "# make_t config; DO NOT add empty lines",
                "language="     + cfg_.language,
                "compiler="     + compiler_to_name(cfg_.compiler_kind),
                "source_file="  + cfg_.source_file,
                "output_file="  + cfg_.output_file,
                "optimization=" + cfg_.optimization,
            };

            write_t w(path);
            if (w.status() != 0x00) {
                return 0x03;
            }
            return w.write(lines);
        }

        /**
         * @brief Прочитать конфигурацию из файла в формате key=value.
         *
         * Неизвестные ключи → предупреждение. Неверные значения enum/флагов
         * → ошибка (поле не изменяется). Признак преждевременной остановки
         * на пустой строке (статус @c 0x02) → предупреждение.
         *
         * @param path Путь к файлу конфигурации.
         * @param rep Если не nullptr — сюда складываются ошибки и предупреждения.
         * @return @c 0x00 / @c 0x03 / @c 0x05.
         */
        int load(const std::string& path, load_report* rep = nullptr) {
            auto add_error = [rep](const std::string& e) {
                if (rep) rep->errors.push_back(e);
            };
            auto add_warn = [rep](const std::string& w) {
                if (rep) rep->warnings.push_back(w);
            };

            read_t reader;
            int read_st = reader.read(path);

            if (read_st == 0x04) {
                return 0x03;
            }
            if (read_st == 0x03) {
                return 0x05;
            }
            if (read_st == 0x02) {
                add_warn("файл содержит пустую строку — чтение остановлено");
            }

            for (const auto& line : reader.data()) {
                if (line.empty() || line[0] == '#') {
                    continue;
                }

                const auto eq = line.find('=');
                if (eq == std::string::npos) {
                    add_warn("строка без '=': " + line);
                    continue;
                }

                const std::string key   = line.substr(0, eq);
                const std::string value = line.substr(eq + 1);

                if (key == "language") {
                    cfg_.language = value;
                } else if (key == "compiler") {
                    if (value == "GCC" || value == "GPP" || value == "CLANG") {
                        cfg_.compiler_kind = name_to_compiler(value);
                    } else {
                        add_error("compiler: неизвестное значение '" + value + "'");
                    }
                } else if (key == "source_file") {
                    if (has_shell_meta(value)) {
                        add_error("source_file: содержит shell-метасимволы");
                    } else {
                        cfg_.source_file = value;
                    }
                } else if (key == "output_file") {
                    if (has_shell_meta(value)) {
                        add_error("output_file: содержит shell-метасимволы");
                    } else {
                        cfg_.output_file = value;
                    }
                } else if (key == "optimization") {
                    if (valid_optimization(value)) {
                        cfg_.optimization = value;
                    } else {
                        add_error("optimization: недопустимый флаг '" + value + "'");
                    }
                } else {
                    add_warn("неизвестный ключ '" + key + "'");
                }
            }

            return 0x00;
        }
};

#endif // MAKE_HPP