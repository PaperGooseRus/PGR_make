#ifndef MAKE_HPP
#define MAKE_HPP

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include "builtin_flags.hpp"
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "process_runner.hpp"
#include "toolchain.hpp"
#include "read.hpp"
#include "write.hpp"

/**
 * @file make.hpp
 * @brief Сборка команды компилятора, каталог флагов, персистентность.
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
 * Читается через std::ifstream напрямую, чтобы пустые строки внутри
 * файла не обрывали чтение.
 */
class flag_catalog_t {
    private:
        std::vector<flag_group_t> groups_;

    public:
        /**
         * @brief Загрузить все группы из каталога.
         * @param dir Каталог с *.txt (например, "config").
         * @return @c 0x00 если загружена хотя бы одна группа,
         *         @c 0x07 если ничего не загружено.
         */
        int load(const std::string& dir = "") {
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
                const std::string filename = dir.empty() ? "" : dir + "/" + g.name + ".txt";
                std::ifstream file(filename);
                std::istringstream embedded(pgr_builtin::flags(g.name));
                std::istream& in = file ? static_cast<std::istream&>(file)
                                         : static_cast<std::istream&>(embedded);

                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty() || line[0] == '#') continue;

                    const auto pos = line.find('|');
                    if (pos == std::string::npos) {
                        g.entries.push_back({line, ""});
                    } else {
                        g.entries.push_back({line.substr(0, pos),
                                             line.substr(pos + 1)});
                    }
                }

                if (!g.entries.empty()) ++loaded;
            }

            return loaded > 0 ? 0x00 : 0x07;
        }

        int load_builtin() { return load(""); }

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
 * @c source_files — список исходных .cpp, которые передаются одной
 * командой (one-shot build). Функция @c main() должна быть ровно
 * в одном из них, иначе линковка упадёт.
 *
 * Соглашение о кодах возврата:
 *  - @c 0x00 успех
 *  - @c 0x01 пустая команда
 *  - @c 0x02 ошибка выполнения
 *  - @c 0x03 файл не открылся (save)
 *  - @c 0x04 файл не найден (load)
 *  - @c 0x05 ошибка чтения (load)
 *  - @c 0x06 некорректная конфигурация (пустой компилятор/исходник,
 *    shell-метасимволы, несовместимые санитайзеры)
 */
class make_t {
    public:
        /**
         * @brief Полная конфигурация сборки.
         */
        struct config_t {
            std::string compiler      = "g++";   ///< Команда компилятора.
            std::vector<std::string> source_files; ///< Все .cpp проекта.
            std::string output_file;             ///< Выходной файл.

            std::string standard;                ///< -std=c++NN
            std::string optimization;            ///< -O*
            std::string debug;                   ///< -g*
            std::string lto;                     ///< -flto*
            std::string arch;                    ///< -march/-mtune

            std::vector<std::string> warnings;       ///< -W*
            std::vector<std::string> sanitizers;     ///< -fsanitize=*
            std::vector<std::string> specific_gcc;   ///< только для GCC
            std::vector<std::string> specific_clang; ///< только для Clang

            std::vector<std::string> include_dirs;   ///< -I<dir>
            std::vector<std::string> library_dirs;   ///< -L<dir>
            std::vector<std::string> libraries;      ///< -l<name> или готовый флаг

            std::vector<std::string> package_dirs; ///< Additional CMAKE_PREFIX_PATH directories
            std::vector<std::string> packages; ///< "name" или "name|target::name"
            std::string profile;               ///< "debug" / "release" / "" (custom)
            std::string toolchain_file;        ///< CMake toolchain for cross-compilation
            std::string c_standard;            ///< e.g. c11 / -std=c11
            std::string linker_script;         ///< embedded linker script (.ld)
            std::string target_type;           ///< static/shared/object/interface for libraries
            std::string toolchain_prefix;     ///< e.g. arm-none-eabi-
            std::string objcopy;              ///< optional objcopy executable
            std::string package_manager;      ///< none / vcpkg / conan
            std::string compiler_id, cmake_id, ninja_id, objcopy_id; ///< pinned Pack entries
            bool operator==(const config_t& o) const {
                return std::tie(compiler, source_files, output_file, standard, optimization, debug, lto, arch, warnings, sanitizers, specific_gcc, specific_clang, include_dirs, library_dirs, libraries, packages, package_dirs, profile, toolchain_file, c_standard, linker_script, target_type, toolchain_prefix, objcopy, package_manager, compiler_id, cmake_id, ninja_id, objcopy_id) ==
                    std::tie(o.compiler, o.source_files, o.output_file, o.standard, o.optimization, o.debug, o.lto, o.arch, o.warnings, o.sanitizers, o.specific_gcc, o.specific_clang, o.include_dirs, o.library_dirs, o.libraries, o.packages, o.package_dirs, o.profile, o.toolchain_file, o.c_standard, o.linker_script, o.target_type, o.toolchain_prefix, o.objcopy, o.package_manager, o.compiler_id, o.cmake_id, o.ninja_id, o.objcopy_id);
            }
            bool operator!=(const config_t& o) const { return !(*this == o); }
        };

    private:
        config_t cfg_;
        std::vector<std::string> args_;
        std::string command_;

        /// @brief Кавычить строку, если в ней есть пробелы или табы.
        static std::string quote_if_needed(const std::string& s) { return s; }
        static bool safe_flag(const std::string& flag) {
            // ProcessRunner passes this as a single argv element: no shell parsing.
            // Permit valid definitions (-DNAME="some value") and linker flags.
            if (flag.empty() || flag[0] != '-') return false;
            return std::none_of(flag.begin(), flag.end(), [](unsigned char ch) {
                return ch == 0 || ch == '\r' || ch == '\n';
            });
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
            // API name retained for compatibility. Paths are passed directly
            // as argv elements, so only invalid NUL/newline bytes are rejected.
            return s.find_first_of("\r\n") != std::string::npos ||
                s.find('\0') != std::string::npos;
        }

        int validate_configuration(bool require_sources = false) const {
            if (cfg_.compiler.empty() || cfg_.compiler.find_first_of("\r\n") != std::string::npos)
                return 0x06;
            if (require_sources && cfg_.source_files.empty()) return 0x06;
            auto has_san = [this](const std::string& s) {
                return std::find(cfg_.sanitizers.begin(), cfg_.sanitizers.end(), s)
                       != cfg_.sanitizers.end();
            };
            if ((has_san("-fsanitize=address") && has_san("-fsanitize=thread")) ||
                (has_san("-fsanitize=address") && has_san("-fsanitize=memory")) ||
                (has_san("-fsanitize=thread") && has_san("-fsanitize=leak"))) return 0x06;
            if (cfg_.lto == "-flto=thin" && cfg_.compiler.find("clang") == std::string::npos &&
                cfg_.toolchain_file.empty()) return 0x06;
            for (const auto& flag : {cfg_.standard, cfg_.optimization, cfg_.debug,
                                     cfg_.lto, cfg_.arch, cfg_.c_standard})
                if (!flag.empty() && !safe_flag(flag)) return 0x06;
            for (const auto& vec : {cfg_.warnings, cfg_.sanitizers,
                                    cfg_.specific_gcc, cfg_.specific_clang})
                for (const auto& flag : vec) if (!safe_flag(flag)) return 0x06;
            for (const auto& vec : {cfg_.source_files, cfg_.include_dirs,
                                    cfg_.library_dirs, cfg_.libraries, cfg_.package_dirs})
                for (const auto& v : vec)
                    if (v.find_first_of("\r\n") != std::string::npos) return 0x06;
            for (const auto& pkg : cfg_.packages) {
                if (pkg.empty()) return 0x06;
                for (unsigned char c : pkg)
                    if (!std::isalnum(c) && c != '_' && c != '-' && c != '+' &&
                        c != '.' && c != ':' && c != '|' && c != '/') return 0x06;
            }
            if (!cfg_.package_manager.empty() && cfg_.package_manager != "none" &&
                cfg_.package_manager != "vcpkg" && cfg_.package_manager != "conan") return 0x06;
            if (!cfg_.target_type.empty() && cfg_.target_type != "static" &&
                cfg_.target_type != "shared" && cfg_.target_type != "object" &&
                cfg_.target_type != "interface") return 0x06;
            if (cfg_.toolchain_prefix.find_first_of("\r\n") != std::string::npos ||
                cfg_.objcopy.find_first_of("\r\n") != std::string::npos ||
                cfg_.package_manager.find_first_of("\r\n") != std::string::npos ||
                cfg_.toolchain_file.find_first_of("\r\n") != std::string::npos ||
                cfg_.linker_script.find_first_of("\r\n") != std::string::npos ||
                cfg_.output_file.find_first_of("\r\n") != std::string::npos)
                return 0x06;
            return 0;
        }

        /// @name Сеттеры конфигурации
        /// @{
        void set_compiler(const std::string& v)     { cfg_.compiler      = v; }
        void set_output_file(const std::string& v)  { cfg_.output_file   = v; }
        void set_standard(const std::string& v)     { cfg_.standard      = v; }
        void set_optimization(const std::string& v) { cfg_.optimization  = v; }
        void set_debug(const std::string& v)        { cfg_.debug         = v; }
        void set_lto(const std::string& v)          { cfg_.lto           = v; }
        void set_arch(const std::string& v)         { cfg_.arch          = v; }

        void set_source_files(const std::vector<std::string>& v)   { cfg_.source_files   = v; }
        void set_warnings(const std::vector<std::string>& v)       { cfg_.warnings       = v; }
        void set_sanitizers(const std::vector<std::string>& v)     { cfg_.sanitizers     = v; }
        void set_specific_gcc(const std::vector<std::string>& v)   { cfg_.specific_gcc   = v; }
        void set_specific_clang(const std::vector<std::string>& v) { cfg_.specific_clang = v; }
        void set_include_dirs(const std::vector<std::string>& v)   { cfg_.include_dirs   = v; }
        void set_library_dirs(const std::vector<std::string>& v)   { cfg_.library_dirs   = v; }
        void set_libraries(const std::vector<std::string>& v)      { cfg_.libraries      = v; }
    
        void set_packages(const std::vector<std::string>& v) { cfg_.packages = v; }
        void set_package_dirs(const std::vector<std::string>& v) { cfg_.package_dirs = v; }
        void set_profile(const std::string& v)               { cfg_.profile  = v; }
        void set_toolchain_file(const std::string& v)        { cfg_.toolchain_file = v; }
        void set_c_standard(const std::string& v)            { cfg_.c_standard = v; }
        void set_linker_script(const std::string& v)         { cfg_.linker_script = v; }
        void set_target_type(const std::string& v)           { cfg_.target_type = v; }
        void set_toolchain_prefix(const std::string& v) { cfg_.toolchain_prefix = v; }
        void set_objcopy(const std::string& v) { cfg_.objcopy = v; }
        void set_package_manager(const std::string& v) { cfg_.package_manager = v; }
        void set_pins(const std::string& cc,const std::string& cm,const std::string& nj,const std::string& oc) {
            cfg_.compiler_id=cc;cfg_.cmake_id=cm;cfg_.ninja_id=nj;cfg_.objcopy_id=oc;
        }
        /// @}

        /// @brief Доступ к текущей конфигурации.
        const config_t& config() const { return cfg_; }

        /**
         * @brief Собрать @ref args_ из @ref cfg_ с полной валидацией.
         *
         * Порядок аргументов:
         * `compiler → standard → optimization → debug → warnings
         *  → sanitizers → lto → arch → specific_<family>
         *  → -I... → все source_files → -L... → -l... → -o output`
         *
         * Проверяет:
         *  - непустые компилятор и список исходников;
         *  - отсутствие shell-метасимволов в компиляторе и путях;
         *  - совместимость санитайзеров;
         *  - соответствие специфичных флагов компилятору (предупреждение).
         *
         * @return @c 0x00 при успехе, @c 0x06 при критической ошибке.
         */
        int build_args() {
            args_.clear();

            // 1. Критические проверки.
            if (validate_configuration(true) != 0) {
                std::cerr << "make_t: invalid configuration\n";
                return 0x06;
            }
            if (cfg_.compiler.empty()) {
                std::cerr << "make_t: compiler is empty\n";
                return 0x06;
            }
            if (has_shell_meta(cfg_.compiler)) {
                std::cerr << "make_t: compiler contains shell metacharacters\n";
                return 0x06;
            }
            if (cfg_.source_files.empty()) {
                std::cerr << "make_t: source_files is empty\n";
                return 0x06;
            }
            if (has_shell_meta(cfg_.output_file)) {
                std::cerr << "make_t: output path contains shell metacharacters\n";
                return 0x06;
            }

            for (const auto& src : cfg_.source_files) {
                if (src.empty()) continue;
                if (has_shell_meta(src)) {
                    std::cerr << "make_t: source path contains shell "
                                 "metacharacters: " << src << '\n';
                    return 0x06;
                }
            }

            // Validate ALL compiler switches, including imported configs.
            std::vector<std::string> flags;
            for (const auto& f : {cfg_.standard, cfg_.optimization, cfg_.debug,
                                  cfg_.lto, cfg_.arch, cfg_.c_standard})
                if (!f.empty()) flags.push_back(f);
            for (const auto& group : {cfg_.warnings, cfg_.sanitizers,
                                     cfg_.specific_gcc, cfg_.specific_clang})
                flags.insert(flags.end(), group.begin(), group.end());
            for (const auto& flag : flags) {
                if (!safe_flag(flag)) {
                    std::cerr << "make_t: invalid compiler option: " << flag << '\n';
                    return 0x06;
                }
            }
            // 2. Проверка несовместимых санитайзеров.
            auto has_san = [this](const std::string& s) {
                return std::find(cfg_.sanitizers.begin(),
                                 cfg_.sanitizers.end(), s)
                       != cfg_.sanitizers.end();
            };
            if (has_san("-fsanitize=address") && has_san("-fsanitize=thread")) {
                std::cerr << "make_t: -fsanitize=address and "
                             "-fsanitize=thread are incompatible\n";
                return 0x06;
            }
            if (has_san("-fsanitize=address") && has_san("-fsanitize=memory")) {
                std::cerr << "make_t: -fsanitize=address and "
                             "-fsanitize=memory are incompatible\n";
                return 0x06;
            }
            if (has_san("-fsanitize=thread") && has_san("-fsanitize=leak")) {
                std::cerr << "make_t: -fsanitize=thread and "
                             "-fsanitize=leak are incompatible\n";
                return 0x06;
            }

            // 3. Compiler backend. MSVC uses entirely different CLI syntax.
            if (pgr_toolchain::classify(cfg_.compiler) == pgr_toolchain::family::msvc) {
                if (!cfg_.sanitizers.empty() || !cfg_.specific_gcc.empty() ||
                    !cfg_.specific_clang.empty() || !cfg_.lto.empty() ||
                    !cfg_.arch.empty() || !cfg_.linker_script.empty()) {
                    std::cerr << "MSVC: unsupported GCC-specific options in this profile\n";
                    return 0x06;
                }
                args_ = {cfg_.compiler, "/nologo", "/EHsc", "/W4"};
                if (cfg_.optimization == "-O2" || cfg_.optimization == "-O3") args_.push_back("/O2");
                else if (cfg_.optimization == "-O1" || cfg_.optimization == "-Os") args_.push_back("/O1");
                else args_.push_back("/Od");
                if (!cfg_.debug.empty() && cfg_.debug != "-g0") args_.push_back("/Zi");
                if (!cfg_.standard.empty()) {
                    auto at=cfg_.standard.find("c++");
                    if (at != std::string::npos) args_.push_back("/std:c++" + cfg_.standard.substr(at+3));
                }
                for (const auto& dir:cfg_.include_dirs) args_.push_back("/I" + dir);
                for (const auto& src:cfg_.source_files)
                    args_.push_back((std::filesystem::u8path(src).extension() == ".c" ? "/Tc" : "/Tp") + src);
                if (!cfg_.output_file.empty()) args_.push_back("/Fe:" + cfg_.output_file);
                if (!cfg_.library_dirs.empty() || !cfg_.libraries.empty()) {
                    args_.push_back("/link");
                    for (const auto& dir:cfg_.library_dirs) args_.push_back("/LIBPATH:" + dir);
                    for (const auto& lib:cfg_.libraries) args_.push_back(lib);
                }
                return 0;
            }
            args_.push_back(pgr_toolchain::selected_compiler(cfg_.compiler, cfg_.toolchain_prefix));

            auto add = [this](const std::string& s) {
                if (!s.empty()) args_.push_back(s);
            };

            const bool c_only = std::all_of(cfg_.source_files.begin(),
                cfg_.source_files.end(), [](const std::string& p) {
                    return std::filesystem::path(p).extension() == ".c";
                });
            if (c_only) add(cfg_.c_standard);
            else add(cfg_.standard);
            add(cfg_.optimization);
            add(cfg_.debug);

            for (const auto& f : cfg_.warnings)   args_.push_back(f);
            for (const auto& f : cfg_.sanitizers) args_.push_back(f);

            add(cfg_.lto);
            add(cfg_.arch);

            const bool is_clang =
                cfg_.compiler.find("clang") != std::string::npos;
            if (is_clang) {
                for (const auto& f : cfg_.specific_clang) args_.push_back(f);
                if (!cfg_.specific_gcc.empty()) {
                    std::cerr << "make_t: warning: GCC-specific flags will "
                                 "be ignored for clang\n";
                }
            } else {
                for (const auto& f : cfg_.specific_gcc) args_.push_back(f);
                if (!cfg_.specific_clang.empty()) {
                    std::cerr << "make_t: warning: Clang-specific flags will "
                                 "be ignored for GCC\n";
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

            // 5. Все исходники подряд.
            for (const auto& src : cfg_.source_files) {
                if (src.empty()) continue;
                args_.push_back(quote_if_needed(src));
            }

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
                if (lib[0] == '-' || lib.find('/') != std::string::npos ||
                    lib.find('\\') != std::string::npos ||
                    std::filesystem::path(lib).has_extension()) args_.push_back(lib);
                else args_.push_back("-l" + lib);
            }

            if (!cfg_.linker_script.empty()) {
                args_.push_back("-T");
                args_.push_back(cfg_.linker_script);
            }

            // 8. Вывод.
            if (!cfg_.output_file.empty()) {
                args_.push_back("-o");
                args_.push_back(quote_if_needed(cfg_.output_file));
            }

            return 0x00;
        }

        /// @brief Доступ к аргументам командной строки.
        const std::vector<std::string>& args() const { return args_; }

        /**
         * @brief Склеить вектор строк в @ref command_ через пробел.
         * @param v Аргументы для склейки.
         * @return @c 0x00 при успехе.
         */
        int build_command(const std::vector<std::string>& v) {
            command_ = pgr_process::display(v);
            return 0x00;
        }

        /// @brief Доступ к последней собранной строке команды.
        const std::string& command() const { return command_; }

        /// @brief Записать аргументы через переданный @p out.
        int write_args(write_t& out) const    { return out.write(args_); }

        /// @brief Записать строку команды через @p out.
        int write_command(write_t& out) const { return out.write(command_); }

        /**
         * @brief Выполнить @ref command_ через std::system.
         * @return @c 0x00 при успехе, @c 0x01 если @ref command_ пуста,
         *         @c 0x02 если команда завершилась с ошибкой.
         */
        int run(const std::function<void(const std::string&)>& on_output = {},
                pgr_process::cancel_token* cancellation = nullptr,
                const std::filesystem::path& directory = {}) {
            if (args_.empty()) {
                std::cerr << "make_t: no command arguments (call build_args)\n";
                return 0x01;
            }
            auto execute = [&](const std::vector<std::string>& arguments) {
                pgr_process::options opt; opt.output=on_output; opt.cancel=cancellation;
                opt.working_directory=directory; return pgr_process::run_ex(arguments,opt);
            };
            if (pgr_toolchain::classify(cfg_.compiler) == pgr_toolchain::family::msvc) {
                const int rc=execute(args_);
                return rc==0 ? 0x00 : 0x02;
            }
            // g++ treats .c as C++, so compile C translation units with gcc/clang
            // into independent objects, then let the C++ compiler link everything.
            std::vector<std::string> c_sources;
            bool has_cpp = false;
            for (const auto& src : cfg_.source_files) {
                if (std::filesystem::u8path(src).extension() == ".c") c_sources.push_back(src);
                else has_cpp = true;
            }
            std::vector<std::string> link_args = args_;
            const std::string cc = pgr_toolchain::c_compiler(
                pgr_toolchain::selected_compiler(cfg_.compiler, cfg_.toolchain_prefix));
            if (!c_sources.empty() && (cc.empty() || !pgr_process::available(cc))) {
                std::cerr << "make_t: matching C compiler not found: " << cc << '\n';
                return 0x02;
            }
            if (!has_cpp && !c_sources.empty()) {
                link_args.front() = cc;
            } else if (!c_sources.empty()) {
                namespace fs = std::filesystem;
                const fs::path output = cfg_.output_file.empty() ? fs::u8path("a.out") : fs::u8path(cfg_.output_file);
                const fs::path objects_dir = (output.has_parent_path() ? output.parent_path() : fs::path(".")) /
                    fs::u8path(".pgr_objects_" + output.filename().u8string());
                std::error_code ec;
                fs::create_directories(objects_dir, ec);
                if (ec) { std::cerr << "make_t: cannot create object folder: " << ec.message() << '\n'; return 0x02; }
                std::vector<std::string> objects;
                const bool clang = cfg_.compiler.find("clang") != std::string::npos;
                for (size_t i = 0; i < c_sources.size(); ++i) {
                    fs::path obj = objects_dir / std::to_string(i);
#ifdef _WIN32
                    obj += ".obj";
#else
                    obj += ".o";
#endif
                    std::vector<std::string> cmd = {cc};
                    auto add = [&](const std::string& flag) { if (!flag.empty()) cmd.push_back(flag); };
                    add(cfg_.c_standard); add(cfg_.optimization); add(cfg_.debug);
                    for (const auto& flag : cfg_.warnings) add(flag);
                    for (const auto& flag : cfg_.sanitizers) add(flag);
                    add(cfg_.lto); add(cfg_.arch);
                    for (const auto& flag : clang ? cfg_.specific_clang : cfg_.specific_gcc) add(flag);
                    for (const auto& path : cfg_.include_dirs) add("-I" + path);
                    cmd.insert(cmd.end(), {"-c", c_sources[i], "-o", obj.u8string()});
                    std::cout << "Executing: " << pgr_process::display(cmd) << '\n' << std::flush;
                    if (execute(cmd) != 0) return 0x02;
                    objects.push_back(obj.u8string());
                }
                std::vector<std::string> updated;
                for (const auto& item : link_args) {
                    if (std::find(c_sources.begin(), c_sources.end(), item) == c_sources.end())
                        updated.push_back(item);
                }
                // Put objects before the library arguments to satisfy one-pass linkers.
                auto pos = std::find_if(updated.begin(), updated.end(), [](const std::string& v) {
                    return v.rfind("-l", 0) == 0 || v.rfind("-L", 0) == 0;
                });
                updated.insert(pos, objects.begin(), objects.end());
                link_args = std::move(updated);
            }
            std::cout << "Executing: " << pgr_process::display(link_args) << '\n' << std::flush;
            const int rc = execute(link_args);
            if (rc != 0) {
                std::cerr << "make_t: compiler failed, rc=" << rc << '\n';
                return 0x02;
            }
            return 0x00;
        }

        // ------------------------------------------------------------------
        // Персистентность конфигурации
        // ------------------------------------------------------------------

        /**
         * @brief Сохранить конфигурацию в файл в формате key=value.
         *
         * Multi-поля (source_files, warnings, sanitizers, specific_*,
         * include_dirs, library_dirs, libraries) пишутся повторяющимися
         * строками с одним ключом:
         * @code
         * compiler=g++
         * source_files=main.cpp
         * source_files=util.cpp
         * warnings=-Wall
         * warnings=-Wextra
         * @endcode
         * Пустые поля (single) не пишутся.
         *
         * @param path Путь к файлу (например, "project/config.cfg").
         * @return @c 0x00 при успехе, @c 0x03 если файл не открылся,
         *         @c 0x02 при ошибке записи.
         */
        int save(const std::string& path) const {
            if (path.empty()) return 0x03;

            std::vector<std::string> lines;
            lines.push_back("# PGR_make config; key=value; multi via repeats");

            lines.push_back("compiler=" + cfg_.compiler);

            for (const auto& s : cfg_.source_files)
                lines.push_back("source_files=" + s);

            if (!cfg_.output_file.empty())
                lines.push_back("output_file="  + cfg_.output_file);
            if (!cfg_.standard.empty())
                lines.push_back("standard="     + cfg_.standard);
            if (!cfg_.optimization.empty())
                lines.push_back("optimization=" + cfg_.optimization);
            if (!cfg_.debug.empty())
                lines.push_back("debug="        + cfg_.debug);
            if (!cfg_.lto.empty())
                lines.push_back("lto="          + cfg_.lto);
            if (!cfg_.arch.empty())
                lines.push_back("arch="         + cfg_.arch);
            if (!cfg_.profile.empty())
                lines.push_back("profile=" + cfg_.profile);
            if (!cfg_.toolchain_file.empty()) lines.push_back("toolchain_file=" + cfg_.toolchain_file);
            if (!cfg_.c_standard.empty()) lines.push_back("c_standard=" + cfg_.c_standard);
            if (!cfg_.linker_script.empty()) lines.push_back("linker_script=" + cfg_.linker_script);
            if (!cfg_.target_type.empty()) lines.push_back("target_type=" + cfg_.target_type);
            if (!cfg_.toolchain_prefix.empty()) lines.push_back("toolchain_prefix=" + cfg_.toolchain_prefix);
            if (!cfg_.objcopy.empty()) lines.push_back("objcopy=" + cfg_.objcopy);
            if (!cfg_.package_manager.empty()) lines.push_back("package_manager=" + cfg_.package_manager);
            if (!cfg_.compiler_id.empty()) lines.push_back("compiler_id=" + cfg_.compiler_id);
            if (!cfg_.cmake_id.empty()) lines.push_back("cmake_id=" + cfg_.cmake_id);
            if (!cfg_.ninja_id.empty()) lines.push_back("ninja_id=" + cfg_.ninja_id);
            if (!cfg_.objcopy_id.empty()) lines.push_back("objcopy_id=" + cfg_.objcopy_id);

            for (const auto& w : cfg_.warnings)
                lines.push_back("warnings="       + w);
            for (const auto& s : cfg_.sanitizers)
                lines.push_back("sanitizers="     + s);
            for (const auto& s : cfg_.specific_gcc)
                lines.push_back("specific_gcc="   + s);
            for (const auto& s : cfg_.specific_clang)
                lines.push_back("specific_clang=" + s);
            for (const auto& s : cfg_.include_dirs)
                lines.push_back("include_dirs="   + s);
            for (const auto& s : cfg_.library_dirs)
                lines.push_back("library_dirs="   + s);
            for (const auto& s : cfg_.libraries)
                lines.push_back("libraries="      + s);
            for (const auto& p : cfg_.packages)
                lines.push_back("packages=" + p);
            for (const auto& p : cfg_.package_dirs)
                lines.push_back("package_dirs=" + p);

            write_t w(path);
            if (w.status() != 0x00) return 0x03;
            return w.write(lines);
        }

        /**
         * @brief Загрузить конфигурацию из файла (полная замена).
         *
         * Файл читается напрямую через std::ifstream — пустые строки
         * внутри не обрывают чтение. Неизвестные ключи игнорируются.
         * Отсутствующие ключи оставляют поле пустым (не мерджит с
         * текущим состоянием).
         *
         * @param path Путь к файлу.
         * @return @c 0x00 при успехе, @c 0x04 если файл не открылся,
         *         @c 0x05 при ошибке чтения.
         */
        int load(const std::string& path) {
            std::ifstream in(std::filesystem::u8path(path));
            if (!in) return 0x04;

            config_t fresh;

            std::string line;
            bool recognized = false;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;

                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;

                const std::string key = line.substr(0, eq);
                const std::string val = line.substr(eq + 1);
                if (key == "compiler" || key == "source_files" || key == "standard" ||
                    key == "output_file" || key == "optimization" || key == "debug" ||
                    key == "warnings" || key == "sanitizers" || key == "lto" ||
                    key == "arch" || key == "specific_gcc" || key == "specific_clang" ||
                    key == "include_dirs" || key == "library_dirs" || key == "libraries" ||
                    key == "profile" || key == "packages" || key == "package_dirs" || key == "toolchain_file" ||
                    key == "c_standard" || key == "linker_script" ||
                    key == "target_type" || key == "toolchain_prefix" ||
                    key == "objcopy" || key == "package_manager" || key == "compiler_id" ||
                    key == "cmake_id" || key == "ninja_id" || key == "objcopy_id") recognized = true;

                if      (key == "compiler")       fresh.compiler      = val;
                else if (key == "source_files")   fresh.source_files.push_back(val);
                else if (key == "output_file")    fresh.output_file   = val;
                else if (key == "standard")       fresh.standard      = val;
                else if (key == "optimization")   fresh.optimization  = val;
                else if (key == "debug")          fresh.debug         = val;
                else if (key == "lto")            fresh.lto           = val;
                else if (key == "arch")           fresh.arch          = val;
                else if (key == "warnings")       fresh.warnings.push_back(val);
                else if (key == "sanitizers")     fresh.sanitizers.push_back(val);
                else if (key == "specific_gcc")   fresh.specific_gcc.push_back(val);
                else if (key == "specific_clang") fresh.specific_clang.push_back(val);
                else if (key == "include_dirs")   fresh.include_dirs.push_back(val);
                else if (key == "library_dirs")   fresh.library_dirs.push_back(val);
                else if (key == "libraries")      fresh.libraries.push_back(val);
                else if (key == "profile")  fresh.profile = val;
                else if (key == "packages") fresh.packages.push_back(val);
                else if (key == "package_dirs") fresh.package_dirs.push_back(val);
                else if (key == "toolchain_file") fresh.toolchain_file = val;
                else if (key == "c_standard") fresh.c_standard = val;
                else if (key == "linker_script") fresh.linker_script = val;
                else if (key == "target_type") fresh.target_type = val;
                else if (key == "toolchain_prefix") fresh.toolchain_prefix = val;
                else if (key == "objcopy") fresh.objcopy = val;
                else if (key == "package_manager") fresh.package_manager = val;
                else if (key == "compiler_id") fresh.compiler_id = val;
                else if (key == "cmake_id") fresh.cmake_id = val;
                else if (key == "ninja_id") fresh.ninja_id = val;
                else if (key == "objcopy_id") fresh.objcopy_id = val;
            }

            if (in.bad()) return 0x05;
            if (!recognized || fresh.compiler.empty()) return 0x06;
            make_t checked;
            checked.cfg_ = fresh;
            if (checked.validate_configuration() != 0) return 0x06;

            cfg_ = std::move(fresh);
            return 0x00;
        }
};

#endif // MAKE_HPP