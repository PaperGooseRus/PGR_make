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
 * @brief Сборка команды компилятора и каталог допустимых флагов.
 */

/**
 * @brief Группа флагов из config/<name>.txt.
 */
struct flag_group_t {
    std::string name;                ///< Имя файла без .txt ("compiler", "optimization", ...)
    std::string title;               ///< Заголовок для TUI
    bool single = true;              ///< true = выбрать один, false = несколько
    std::vector<std::string> flags;  ///< Допустимые значения
};

/**
 * @class flag_catalog_t
 * @brief Каталог допустимых флагов, загружаемый из каталога config/.
 */
class flag_catalog_t {
    private:
        std::vector<flag_group_t> groups_;

    public:
        /**
         * @brief Загрузить все группы из каталога.
         *
         * Порядок групп определяет порядок меню. Отсутствующие файлы
         * пропускаются с предупреждением в std::cerr. Пустая строка
         * в файле (согласно семантике @ref read_t) останавливает
         * чтение этого файла — пользователь должен избегать пустых
         * строк в конфигах.
         *
         * @param dir Каталог с *.txt (например, "config").
         * @return @c 0x00 всегда (ошибки — предупреждениями).
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

            for (auto& g : groups_) {
                read_t r;
                const std::string path = dir + "/" + g.name + ".txt";
                int st = r.read(path);

                if (st == 0x04) {
                    std::cerr << "catalog: cannot open " << path << '\n';
                    continue;
                }
                if (st == 0x02) {
                    std::cerr << "catalog: " << path
                              << " contains empty line, reading stopped\n";
                }

                for (const auto& line : r.data()) {
                    if (line.empty() || line[0] == '#') {
                        continue;
                    }
                    g.flags.push_back(line);
                }
            }

            return 0x00;
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
 *    shell-метасимволы в путях)
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

            std::vector<std::string> warnings;       ///< -W*
            std::vector<std::string> sanitizers;     ///< -fsanitize=*
            std::vector<std::string> specific_gcc;   ///< только для GCC
            std::vector<std::string> specific_clang; ///< только для Clang
        };

    private:
        config_t cfg_;
        std::vector<std::string> args_;
        std::string command_;

        static bool has_shell_meta(const std::string& s) {
            return s.find_first_of(";|&`$<>\"\n\r") != std::string::npos;
        }

        static std::string quote_if_needed(const std::string& s) {
            if (s.find_first_of(" \t") == std::string::npos) {
                return s;
            }
            return "\"" + s + "\"";
        }

    public:
        make_t() = default;
        ~make_t() = default;

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
        /// @}

        const config_t& config() const { return cfg_; }

        /**
         * @brief Собрать @ref args_ из @ref cfg_ в правильном порядке.
         *
         * Порядок:
         * `compiler -> standard -> optimization -> debug -> warnings
         * -> sanitizers -> lto -> arch -> specific_<family>
         * -> source_file -> -o -> output_file`
         *
         * Специфичные флаги выбираются по имени компилятора:
         * если в @c compiler есть "clang" — добавляются @c specific_clang,
         * иначе — @c specific_gcc.
         *
         * @return @c 0x00 при успехе, @c 0x06 при некорректной конфигурации.
         */
        int build_args() {
            args_.clear();

            if (cfg_.compiler.empty()) {
                std::cerr << "make_t: compiler is empty\n";
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
            } else {
                for (const auto& f : cfg_.specific_gcc)   args_.push_back(f);
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
                std::cerr << "make_t: command failed, rc = " << rc << '\n';
                return 0x02;
            }
            return 0x00;
        }
};

#endif // MAKE_HPP