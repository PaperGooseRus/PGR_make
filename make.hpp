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
 * Типовое использование:
 * @code
 * make_t m;
 * m.set_compiler(make_t::compiler::GPP);
 * m.set_source_file("main.cpp");
 * m.set_output_file("main.exe");
 * m.set_optimization("-Os");
 * m.build_args();          // args_ = { "g++", "-Os", "main.cpp", "-o", "main.exe" }
 * m.build_command(m.args()); // command_ = "g++ -Os main.cpp -o main.exe"
 * m.run();                 // выполняет command_
 * @endcode
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 пустая команда
 *  - @c 0x02 ошибка выполнения команды
 *  - @c 0x03 ошибка открытия файла
 *  - @c 0x04 пустая команда в файле
 *  - @c 0x05 ошибка чтения файла
 */
class make_t {
    public:
        /// @brief Поддерживаемые компиляторы.
        enum class compiler {
            GCC   = 0x00,  ///< gcc (C)
            GPP   = 0x01,  ///< g++ (C++)
            CLANG = 0x02   ///< clang
        };

        /// @brief Конфигурация для сборки команды компилятора.
        struct config_t {
            std::string language      = "";             ///< Имя языка (справочно, в команду не входит).
            compiler    compiler_kind = compiler::GPP;  ///< Выбранный компилятор.
            std::string source_file   = "";             ///< Исходный файл для компиляции.
            std::string output_file   = "";             ///< Путь к выходному исполняемому файлу.
            std::string optimization  = "";             ///< Флаг оптимизации, например "-Os".
        };

    private:
        config_t cfg_;                   ///< Конфигурация сборки.
        std::vector<std::string> args_;  ///< Аргументы командной строки.
        std::string command_;            ///< Склеенная строка команды.

        /**
         * @brief Преобразовать значение enum компилятора в имя исполняемого файла.
         * @param c Значение компилятора.
         * @return Имя исполняемого файла ("gcc", "g++", "clang") либо "unknown".
         */
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

        /// @name Сеттеры конфигурации
        /// @{
        void set_language(const std::string& v)     { cfg_.language      = v; }
        void set_compiler(compiler c)               { cfg_.compiler_kind = c; }
        void set_source_file(const std::string& v)  { cfg_.source_file   = v; }
        void set_output_file(const std::string& v)  { cfg_.output_file   = v; }
        void set_optimization(const std::string& v) { cfg_.optimization  = v; }
        /// @}

        /**
         * @brief Пересобрать @ref args_ из @ref cfg_.
         *
         * Порядок: `<compiler> [optimization] [source_file] [-o output_file]`.
         * Пустые необязательные поля пропускаются.
         *
         * @return @c 0x00 при успехе.
         */
        int build_args() {
            args_.clear();

            args_.push_back(compiler_to_string(cfg_.compiler_kind));

            if (!cfg_.optimization.empty()) {
                args_.push_back(cfg_.optimization);
            }

            if (!cfg_.source_file.empty()) {
                args_.push_back(cfg_.source_file);
            }

            if (!cfg_.output_file.empty()) {
                args_.push_back("-o");
                args_.push_back(cfg_.output_file);
            }

            return 0x00;
        }

        /// @brief Доступ к аргументам командной строки.
        const std::vector<std::string>& args() const {
            return args_;
        }

        /**
         * @brief Склеить вектор строк в @ref command_ через пробел.
         * @param v Аргументы для склейки.
         * @return @c 0x00 при успехе.
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

        /// @brief Доступ к последней собранной строке команды.
        const std::string& command() const {
            return command_;
        }

        // ------------------------------------------------------------------
        // Сохранение через внешний write_t (переиспользуемый объект)
        // ------------------------------------------------------------------

        /**
         * @brief Записать аргументы (@ref args_) через переданный @p out.
         * @param out Готовый объект @ref write_t.
         * @return Код возврата @ref write_t.
         */
        int write_args(write_t& out) const {
            return out.write(args_);
        }

        /**
         * @brief Записать строку команды (@ref command_) через @p out.
         * @param out Готовый объект @ref write_t.
         * @return Код возврата @ref write_t.
         */
        int write_command(write_t& out) const {
            return out.write(command_);
        }

        // ------------------------------------------------------------------
        // Выполнение
        // ------------------------------------------------------------------

        /**
         * @brief Выполнить уже собранную команду из @ref command_.
         * @return @c 0x00 при успехе, @c 0x01 если @ref command_ пуста,
         *         @c 0x02 если команда завершилась с ошибкой.
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
         *
         * Берётся первая непустая строка файла.
         *
         * @param path Путь к файлу с командой.
         * @return @c 0x00 при успехе, @c 0x02 при ошибке выполнения,
         *         @c 0x03 если файл не открылся, @c 0x04 если файл пуст,
         *         @c 0x05 при ошибке чтения.
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
};

#endif // MAKE_HPP