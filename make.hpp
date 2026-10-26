#ifndef MAKE_HPP
#define MAKE_HPP

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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
 * m.set_target_cpp("main.cpp");
 * m.set_target_app("main.exe");
 * m.set_optimization("-Os");
 * m.make_arr();          // arr_ = { "g++", "-Os", "main.cpp", "-o", "main.exe" }
 * m.make(m.data());      // cmd_ = "g++ -Os main.cpp -o main.exe"
 * m.make_run();          // выполняет cmd_
 * @endcode
 *
 * Соглашение о кодах возврата (общее для всех методов):
 *  - @c 0x00 успех
 *  - @c 0x01 пустая команда
 *  - @c 0x02 ошибка выполнения команды
 *  - @c 0x03 ошибка открытия файла
 *  - @c 0x04 пустая команда в файле
 */
class make_t {
    public:
        /**
         * @brief Поддерживаемые компиляторы.
         */
        enum class compiler {
            GCC   = 0x00,  ///< gcc (C)
            GPP   = 0x01,  ///< g++ (C++)
            CLANG = 0x02   ///< clang
        };

        /**
         * @brief Конфигурация для сборки команды компилятора.
         */
        struct config_make_t {
            std::string language     = "";             ///< Имя языка (справочно, в команду не входит).
            compiler    compiler_    = compiler::GPP;  ///< Выбранный компилятор.
            std::string target_cpp   = "";             ///< Исходный файл для компиляции.
            std::string target_app   = "";             ///< Путь к выходному исполняемому файлу.
            std::string optimization = "";             ///< Флаг оптимизации, например "-Os".
        };

    private:
        config_make_t cfg_;              ///< Конфигурация сборки.
        std::vector<std::string> arr_;   ///< Аргументы командной строки.
        std::string cmd_;                ///< Склеенная строка команды.

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
        void set_language(const std::string& v)     { cfg_.language     = v; }
        void set_compiler(compiler c)               { cfg_.compiler_    = c; }
        void set_target_cpp(const std::string& v)   { cfg_.target_cpp   = v; }
        void set_target_app(const std::string& v)   { cfg_.target_app   = v; }
        void set_optimization(const std::string& v) { cfg_.optimization = v; }
        /// @}

        /**
         * @brief Пересобрать @ref arr_ из @ref cfg_.
         *
         * Формирует аргументы в порядке, ожидаемом типичным компилятором:
         * @code
         * <compiler> [optimization] [target_cpp] [-o target_app]
         * @endcode
         * Пустые необязательные поля пропускаются.
         *
         * @return @c 0x00 при успехе.
         */
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

        /**
         * @brief Доступ к аргументам командной строки.
         * @return Константная ссылка на внутренний вектор.
         */
        const std::vector<std::string>& data() const {
            return arr_;
        }

        /**
         * @brief Склеить вектор строк в @ref cmd_ через пробел.
         * @param v Аргументы для склейки.
         * @return @c 0x00 при успехе.
         */
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

        /**
         * @brief Доступ к последней собранной строке команды.
         * @return Константная ссылка на @ref cmd_.
         */
        const std::string& get_cmd() const {
            return cmd_;
        }

        /**
         * @brief Выполнить уже собранную команду из @ref cmd_.
         *
         * Используется std::system(); команда передаётся оболочке ОС.
         *
         * @return @c 0x00 при успехе, @c 0x01 если @ref cmd_ пуста,
         *         @c 0x02 если команда завершилась с ошибкой.
         */
        int make_run() {
            if (cmd_.empty()) {
                std::cerr << "make_t: command is empty\n";
                return 0x01;
            }

            std::cout << "Executing: " << cmd_ << '\n';

            int rc = std::system(cmd_.c_str());
            if (rc != 0) {
                std::cerr << "make_t: command failed, rc = " << rc << '\n';
                return 0x02;
            }

            return 0x00;
        }

        /**
         * @brief Прочитать команду из файла и выполнить её.
         *
         * Читает первую строку @p path в @ref cmd_ и выполняет через
         * std::system().
         *
         * @param path Путь к файлу с командой.
         * @return @c 0x00 при успехе, @c 0x02 при ошибке выполнения,
         *         @c 0x03 если файл не открылся, @c 0x04 если файл пуст.
         */
        int make_run(const std::string& path) {
            std::ifstream in(path);
            if (!in) {
                std::cerr << "make_t: cannot open " << path << '\n';
                return 0x03;
            }

            std::string line;
            if (!std::getline(in, line) || line.empty()) {
                std::cerr << "make_t: empty command in " << path << '\n';
                return 0x04;
            }

            cmd_ = line; // сохраняем, чтобы был доступен через get_cmd()

            std::cout << "Executing: " << cmd_ << '\n';

            int rc = std::system(cmd_.c_str());
            if (rc != 0) {
                std::cerr << "make_t: command failed, rc = " << rc << '\n';
                return 0x02;
            }

            return 0x00;
        }
};

#endif // MAKE_HPP