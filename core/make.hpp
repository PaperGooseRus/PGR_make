#ifndef MAKE_HPP
#define MAKE_HPP

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "read.hpp"
#include "write.hpp"

/**
 * @file make.hpp
 * @brief Сборка команды компилятора с проверками и каталогом флагов.
 */

/**
 * @brief Одна запись каталога: флаг и его пояснение.
 */
struct flag_entry_t {
    std::string flag;         ///< Сам флаг, например "-O2".
    std::string description;  ///< Пояснение для пользователя.
};

/**
 * @brief Группа флагов из config/<name>.txt.
 */
struct flag_group_t {
    std::string name;                   ///< Имя файла без .txt.
    std::string title;                  ///< Заголовок для TUI.
    bool single = true;                 ///< true = выбрать один, false = несколько.
    std::vector<flag_entry_t> entries;  ///< Допустимые флаги с описаниями.
};

/**
 * @class flag_catalog_t
 * @brief Каталог допустимых флагов, загружаемый из каталога config/.
 *
 * Формат строки в файле:
 * @code
 * флаг|пояснение
 * @endcode
 * Если разделитель '|' отсутствует, вся строка считается флагом.
 * Пустые строки и строки, начинающиеся с '#', пропускаются.
 */
class flag_catalog_t {
    private:
        std::vector<flag_group_t> groups_;

    public:
        /**
         * @brief Загрузить все группы из каталога.
         *
         * Читает файлы напрямую через std::ifstream (НЕ через read_t),
         * чтобы пустые строки внутри файла не обрывали чтение.
         *
         * @param dir Каталог с *.txt (например, "config").
         * @return @c 0x00 если загружена хотя бы одна группа,
         *         @c 0x07 если ничего не загружено.
         */
        int load(const std::string& dir) {
            groups_ = {
                {"compiler",       "Компилятор",     true,  {}},
                {"standard",       "Стандарт C++",   true,  {}},
                {"optimization",   "Оптимизация",    true,  {}},
                {"debug",          "Отладка",        true,  {}},
                {"warnings",       "Предупреждения", false, {}},
                {"sanitizers",     "Санитайзеры",    false, {}},
                {"lto",            "LTO",            true,  {}},
                {"arch",           "Архитектура",    true,  {}},
                {"specific_gcc",   "GCC-флаги",      false, {}},
                {"specific_clang", "Clang-флаги",    false, {}},
            };

            int loaded = 0;

            for (auto& g : groups_) {
                const std::string path = dir + "/" + g.name + ".txt";
                std::ifstream in(path);
                if (!in) {
                    std::cerr << "catalog: cannot open " << path << '\n';
                    continue;
                }

                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (line.empty() || line[0] == '#') {
                        continue;
                    }

                    const auto pos = line.find('|');
                    if (pos == std::string::npos) {
                        g.entries.push_back({line, ""});
                    } else {
                        g.entries.push_back({line.substr(0, pos),
                                             line.substr(pos + 1)});
                    }
                }

                if (!g.entries.empty()) {
                    ++loaded;
                }
            }

            return loaded > 0 ? 0x00 : 0x07;
        }

        /// @brief Все группы (в порядке меню).
        const std::vector<flag_group_t>& groups() const { return groups_; }

        /// @brief Найти группу по имени; nullptr если нет.
        const flag_group_t* find(const std::string& name) const {
            for (const auto& g : groups_) {
                if (g.name == name) return &g;
            }
            return nullptr;
        }
};

/**
 * @class make_t
 * @brief Собирает команду компилятора из набора флагов и запускает её.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 пустая команда
 *  - @c 0x02 ошибка выполнения
 *  - @c 0x06 некорректная конфигурация (пустой компилятор/исходник,
 *    shell-метасимволы, несовместимые санитайзеры)
 */
class make_t {
    public:
        /**
         * @brief Полная конфигурация сборки.
         *
         * Single-поля — строки (пустая строка = не задано).
         * Multi-поля — векторы строк.
         */
        struct config_t {
            std::string compiler      = "g++";  ///< Команда компилятора.
            std::string source_file;            ///< Исходный файл.
            std::string output_file;            ///< Выходной файл.

            std::string standard;               ///< -std=c++NN
            std::string optimization;           ///< -O*
            std::string debug;                  ///< -g*
            std::string lto;                    ///< -flto*
            std::string arch;                   ///< -march/-mtune

            std::vector<std::string> warnings;        ///< -W*
            std::vector<std::string> sanitizers;      ///< -fsanitize=*
            std::vector<std::string> specific_gcc;    ///< только для GCC
            std::vector<std::string> specific_clang;  ///< только для Clang

            std::vector<std::string> include_dirs;    ///< -I<dir>
            std::vector<std::string> library_dirs;    ///< -L<dir>
            std::vector<std::string> libraries;       ///< -l<name> или готовый флаг
        };

    private:
        config_t cfg_;
        std::vector<std::string> args_;
        std::string command_;

        static std::string quote_if_needed(const std::string& s) {
            if (s.find_first_of(" \t") == std::string::npos) {
                return s;
            }
            return "\"" + s + "\"";
        }

    public:
        make_t() = default;
        ~make_t() = default;

        /**
         * @brief Проверить, содержит ли строка shell-метасимволы.
         *
         * Используется и внутри make_t, и из TUI — чтобы правила
         * проверки совпадали в обоих местах.
         */
        static bool has_shell_meta(const std::string& s) {
            return s.find_first_of(";|&`$<>\"\n\r") != std::string::npos;
        }

        /// @name Сеттеры конфигурации
        /// @{
        void set_compiler(const std::string& v)     { cfg_.compiler      = v; }
        void set_source_file(const std::string& v)  { cfg_.source_file   = v; }
        void set_output_file(const std::string& v)  { cfg_.output_file   = v; }
        void set_standard(const std::string& v)     { cfg_.standard      = v; }
        void set_optimization(const std::string& v) { cfg_.optimization  = v; }
        void set_debug(const std::string& v)        { cfg_.debug         = v; }
        void set_lto(const std::string& v)          { cfg_.lto           = v; }
        void set_arch(const std::string& v)         { cfg_.arch          = v; }

        void set_warnings(const std::vector<std::string>& v)       { cfg_.warnings       = v; }
        void set_sanitizers(const std::vector<std::string>& v)     { cfg_.sanitizers     = v; }
        void set_specific_gcc(const std::vector<std::string>& v)   { cfg_.specific_gcc   = v; }
        void set_specific_clang(const std::vector<std::string>& v) { cfg_.specific_clang = v; }

        void set_include_dirs(const std::vector<std::string>& v)   { cfg_.include_dirs   = v; }
        void set_library_dirs(const std::vector<std::string>& v)   { cfg_.library_dirs   = v; }
        void set_libraries(const std::vector<std::string>& v)      { cfg_.libraries      = v; }
        /// @}

        const config_t& config() const { return cfg_; }

        /**
         * @brief Собрать @ref args_ из @ref cfg_ с полной валидацией.
         *
         * Порядок:
         * `compiler -> standard -> optimization -> debug -> warnings
         *  -> sanitizers -> lto -> arch -> specific_<family>
         *  -> -I... -> source_file -> -L... -> -l... -> -o output_file`
         *
         * Проверяет:
         *  - непустые компилятор и исходник;
         *  - отсутствие shell-метасимволов в компиляторе и путях;
         *  - совместимость санитайзеров;
         *  - соответствие специфичных флагов компилятору (предупреждение).
         *
         * @return @c 0x00 при успехе, @c 0x06 при критической ошибке.
         */
        int build_args() {
            args_.clear();

            // 1. Критические проверки.
            if (cfg_.compiler.empty()) {
                std::cerr << "make_t: compiler is empty\n";
                return 0x06;
            }
            if (has_shell_meta(cfg_.compiler)) {
                std::cerr << "make_t: compiler contains shell metacharacters\n";
                return 0x06;
            }
            if (cfg_.source_file.empty()) {
                std::cerr << "make_t: source_file is empty\n";
                return 0x06;
            }
            if (has_shell_meta(cfg_.source_file) ||
                has_shell_meta(cfg_.output_file)) {
                std::cerr << "make_t: path contains shell metacharacters\n";
                return 0x06;
            }

            // 2. Проверка несовместимых санитайзеров.
            auto has_san = [this](const std::string& s) {
                return std::find(cfg_.sanitizers.begin(),
                                 cfg_.sanitizers.end(), s)
                       != cfg_.sanitizers.end();
            };
            if (has_san("-fsanitize=address") && has_san("-fsanitize=thread")) {
                std::cerr << "make_t: -fsanitize=address and -fsanitize=thread "
                             "are incompatible\n";
                return 0x06;
            }
            if (has_san("-fsanitize=address") && has_san("-fsanitize=memory")) {
                std::cerr << "make_t: -fsanitize=address and -fsanitize=memory "
                             "are incompatible\n";
                return 0x06;
            }
            if (has_san("-fsanitize=thread") && has_san("-fsanitize=leak")) {
                std::cerr << "make_t: -fsanitize=thread and -fsanitize=leak "
                             "are incompatible\n";
                return 0x06;
            }

            // 3. Сборка в правильном порядке.
            args_.push_back(cfg_.compiler);

            auto add = [this](const std::string& s) {
                if (!s.empty()) args_.push_back(s);
            };

            add(cfg_.standard);
            add(cfg_.optimization);
            add(cfg_.debug);
            for (const auto& f : cfg_.warnings)   args_.push_back(f);
            for (const auto& f : cfg_.sanitizers) args_.push_back(f);
            add(cfg_.lto);
            add(cfg_.arch);

            const bool is_clang = cfg_.compiler.find("clang") != std::string::npos;
            if (is_clang) {
                for (const auto& f : cfg_.specific_clang) args_.push_back(f);
                if (!cfg_.specific_gcc.empty()) {
                    std::cerr << "make_t: warning: GCC-specific flags will be "
                                 "ignored for clang\n";
                }
            } else {
                for (const auto& f : cfg_.specific_gcc) args_.push_back(f);
                if (!cfg_.specific_clang.empty()) {
                    std::cerr << "make_t: warning: Clang-specific flags will be "
                                 "ignored for GCC\n";
                }
            }

            // 4. -I: до исходника.
            for (const auto& dir : cfg_.include_dirs) {
                if (dir.empty()) continue;
                if (has_shell_meta(dir)) {
                    std::cerr << "make_t: include dir contains shell "
                                 "metacharacters: " << dir << '\n';
                    return 0x06;
                }
                args_.push_back("-I" + quote_if_needed(dir));
            }

            // 5. Исходник.
            args_.push_back(quote_if_needed(cfg_.source_file));

            // 6. -L: до -l.
            for (const auto& dir : cfg_.library_dirs) {
                if (dir.empty()) continue;
                if (has_shell_meta(dir)) {
                    std::cerr << "make_t: library dir contains shell "
                                 "metacharacters: " << dir << '\n';
                    return 0x06;
                }
                args_.push_back("-L" + quote_if_needed(dir));
            }

            // 7. -l (или готовый флаг, если начинается с '-').
            for (const auto& lib : cfg_.libraries) {
                if (lib.empty()) continue;
                if (has_shell_meta(lib)) {
                    std::cerr << "make_t: library name contains shell "
                                 "metacharacters: " << lib << '\n';
                    return 0x06;
                }
                if (lib[0] == '-') {
                    args_.push_back(lib);
                } else {
                    args_.push_back("-l" + lib);
                }
            }

            // 8. Вывод.
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
            for (const auto& s : v) total += s.size();
            if (!v.empty()) total += sep.size() * (v.size() - 1);
            command_.reserve(total);

            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) command_ += sep;
                command_ += v[i];
            }
            return 0x00;
        }

        const std::string& command() const { return command_; }

        int write_args(write_t& out) const    { return out.write(args_); }
        int write_command(write_t& out) const { return out.write(command_); }

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
                std::cerr << "make_t: command failed, rc = " << rc << '\n'
                          << "  Check that '" << cfg_.compiler
                          << "' is in PATH and source file exists.\n";
                return 0x02;
            }
            return 0x00;
        }
};

#endif // MAKE_HPP