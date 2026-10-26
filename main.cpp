/**
 * @file main.cpp
 * @brief Точка входа: TUI по умолчанию, headless по флагам.
 */

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include "core/TUI.hpp"
#include "core/cmake_writer.hpp"
#include "core/make.hpp"
#include "core/process_runner.hpp"
#include "core/safe_paths.hpp"
#include "core/tree.hpp"

/**
 * @brief Headless-сборка: scan → layout → save CMakeLists → cmake --build.
 * @param profile Профиль ("" = config.cfg).
 * @param build_dir Директория сборки.
 * @return Код возврата: 0 успех, иначе — ошибка.
 */
static int run_ci(const std::string& profile, const std::string& build_dir) {
    make_t builder;
    std::string cfg_path = "project/config.cfg";
    if (!profile.empty()) {
        // No relative path traversal via a profile name.
        if (profile.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
            std::cerr << "CI: invalid profile name\n";
            return 1;
        }
        cfg_path = "project/profiles/" + profile + ".cfg";
    }
    if (builder.load(cfg_path) != 0) {
        std::cerr << "CI: cannot load valid config " << cfg_path << '\n';
        return 1;
    }
    if (builder.validate_configuration() != 0) {
        std::cerr << "CI: configuration validation failed\n";
        return 1;
    }
    if (!pgr_paths::safe_build_dir(build_dir)) {
        std::cerr << "CI: unsafe build directory " << build_dir << '\n';
        return 1;
    }

    tree_t tree(".");
    tree_t::options_t opts;
    opts.project_name = "PGR_make";
    opts.exclude_dirs.push_back("project");
    opts.exclude_dirs.push_back(std::filesystem::path(build_dir).lexically_normal().generic_string());
    opts.only_sources = builder.config().source_files;
    opts.include_roots = builder.config().include_dirs;
    tree.set_options(opts);
    if (tree.scan() != 0) { std::cerr << "CI: scan failed\n"; return 1; }
    if (!tree.cycles().empty())
        std::cerr << "CI: warning: header/module include cycles detected; build will be attempted\n";

    cmake_layout_t layout;
    if (layout.build(tree) != 0) { std::cerr << "CI: layout failed\n"; return 1; }
    if (cmake_writer_t::save_staged(tree, layout, builder.config()) != 0) {
        std::cerr << "CI: CMake generation failed\n"; return 1;
    }

    std::vector<std::string> configure = {
        "cmake", "-S", cmake_writer_t::staged_root, "-B", build_dir
    };
    if (!std::filesystem::exists(std::filesystem::path(build_dir) / "CMakeCache.txt") &&
        pgr_process::available("ninja")) {
        configure.push_back("-G");
        configure.push_back("Ninja");
    }
    // Set the compiler with -DCMAKE_* before the project() invocation.
    if (!builder.config().compiler.empty() && builder.config().toolchain_file.empty()) {
        configure.push_back("-DCMAKE_CXX_COMPILER=" + builder.config().compiler);
        if (builder.config().compiler == "gcc" || builder.config().compiler == "clang")
            configure.push_back("-DCMAKE_C_COMPILER=" + builder.config().compiler);
    }
    std::vector<std::string> build = {"cmake", "--build", build_dir};
    std::cout << "CI: " << pgr_process::display(configure) << '\n' << std::flush;
    if (pgr_process::run(configure) != 0) return 2;
    std::cout << "CI: " << pgr_process::display(build) << '\n' << std::flush;
    if (pgr_process::run(build) != 0) return 3;

    std::error_code ec;
    const auto cc = std::filesystem::path(build_dir) / "compile_commands.json";
    if (std::filesystem::exists(cc)) {
        std::filesystem::copy_file(cc, "compile_commands.json",
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) std::cerr << "CI: warning: compile_commands copy: " << ec.message() << '\n';
    }
    return 0;
}

static void print_help(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "  (no args)               интерактивный TUI\n"
              << "  --ci [--profile NAME]   headless сборка через CMake\n"
              << "  --build-dir DIR         каталог сборки (по умолч. build_cmake)\n"
              << "  --clean                 удалить build_cmake перед сборкой\n"
              << "  --help                  эта справка\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    bool ci_mode = false;
    bool clean = false;
    std::string profile;
    std::string build_dir = "build_cmake";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { print_help(argv[0]); return 0; }
        else if (a == "--ci") ci_mode = true;
        else if (a == "--clean") clean = true;
        else if (a == "--profile" && i + 1 < argc) profile = argv[++i];
        else if (a == "--build-dir" && i + 1 < argc) build_dir = argv[++i];
        else { std::cerr << "Unknown arg: " << a << '\n'; return 1; }
    }

    if (clean && !ci_mode) {
        std::cerr << "--clean requires --ci\n";
        return 1;
    }
    if (clean) {
        std::string error;
        if (!pgr_paths::clean(build_dir, error)) {
            std::cerr << "clean: " << error << '\n';
            return 1;
        }
    }
    if (ci_mode) return run_ci(profile, build_dir);

    std::filesystem::create_directories("build");
    std::filesystem::create_directories("project");
    tui_t tui;
    tui.run();
    return 0;
}