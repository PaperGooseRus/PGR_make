/**
 * @file main.cpp
 * @brief Portable PGR_Make entrypoint and project selection.
 *
 * Layout relative to PGR_Make.exe:
 *   Pack/               external compilers, CMake, Ninja
 *   Project/<name>/     isolated metadata, flags and logs
 * Sources may be placed anywhere. Existing CMakeLists are never overwritten.
 */
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "core/TUI.hpp"
#include "core/cmake_writer.hpp"
#include "core/make.hpp"
#include "core/pack_tools.hpp"
#include "core/process_runner.hpp"
#include "core/safe_paths.hpp"
#include "core/tree.hpp"
#include "core/toolchain.hpp"
#include "core/cmake_session.hpp"
#include "core/workspace.hpp"

namespace fs = std::filesystem;
using pgr_workspace::project_t;
using pgr_workspace::registry_t;

static bool configure_new(const project_t& project, std::string& error) {
    make_t builder;
    builder.set_compiler("g++");
    builder.set_standard("-std=c++17");
    builder.set_optimization("-O0");
    builder.set_warnings({"-Wall", "-Wextra"});
    const auto sources = pgr_workspace::discover_sources(project.source_root);
    builder.set_source_files(sources);
    const bool cpp = std::any_of(sources.begin(), sources.end(), [](const std::string& file) {
        return fs::u8path(file).extension() != ".c";
    });
    if (!cpp && !sources.empty()) { builder.set_compiler("gcc"); builder.set_c_standard("-std=c11"); }
    builder.set_output_file("build/" + project.name +
#ifdef _WIN32
        ".exe"
#else
        ""
#endif
    );
    if (builder.save(pgr_workspace::utf8(project.config())) != 0) {
        error = "Cannot save project configuration";
        return false;
    }
    return true;
}

static bool create_project(const registry_t& registry, const std::string& name,
                           const fs::path& source, bool starter, project_t& result,
                           std::string& error) {
    if (!registry.create(name, source, result, error)) return false;
    if (starter && !fs::exists(result.source_root / "main.cpp") &&
        !fs::exists(result.source_root / "src/main.cpp")) {
        std::ofstream example(result.source_root / "main.cpp", std::ios::binary);
        example << "#include <iostream>\n\nint main() {\n"
                   "    std::cout << \"Hello from PGR_Make!\\n\";\n"
                   "    return 0;\n}\n";
        example.close();
        if (!example) { error = "Cannot write sample main.cpp"; return false; }
    }
    return configure_new(result, error);
}

static int run_ci(const project_t& project, const std::string& profile,
                  const std::string& build_dir, bool clean, bool native_cmake) {
    make_t builder;
    fs::path config = project.config();
    if (!profile.empty()) {
        if (!pgr_workspace::valid_name(profile)) {
            std::cerr << "CI: invalid profile name\n";
            return 1;
        }
        config = project.profiles() / fs::u8path(profile + ".cfg");
    }
    if (builder.load(pgr_workspace::utf8(config)) != 0) {
        std::cerr << "CI: cannot load config " << pgr_workspace::utf8(config) << '\n';
        return 1;
    }
    if (builder.validate_configuration() != 0) {
        std::cerr << "CI: invalid configuration\n";
        return 1;
    }
    const fs::path target = build_dir.empty() ? project.build() : pgr_paths::absolute_build_dir(fs::u8path(build_dir), project.source_root);
    if (!pgr_paths::safe_build_dir(target, project.source_root)) {
        std::cerr << "CI: unsafe build directory: " << build_dir << '\n';
        return 1;
    }
    if (clean) {
        std::string error;
        if (!pgr_paths::clean(target, project.source_root, project.store_root, error)) {
            std::cerr << "CI clean: " << error << '\n';
            return 1;
        }
    }

    std::string prep_error;
    if (!pgr_paths::prepare(target, project.source_root, project.store_root, prep_error)) {
        std::cerr << "CI: " << prep_error << '\n'; return 1;
    }
    if (native_cmake && !fs::is_regular_file(project.source_root / "CMakeLists.txt")) {
        std::cerr << "CI: no existing CMakeLists.txt in source root\n"; return 1;
    }
    if (!native_cmake && !pgr_cmake::generate(project, builder.config(), prep_error)) {
        std::cerr << "CI: " << prep_error << '\n'; return 1;
    }
    if (!pgr_cmake::prepare_cache(project, target, builder.config(), prep_error, native_cmake)) {
        std::cerr << "CI: " << prep_error << '\n'; return 1;
    }

    std::vector<std::string> configure = {
        "cmake", "-S", pgr_workspace::utf8(native_cmake ? project.source_root : project.stage()), "-B", pgr_workspace::utf8(target)
    };
    if (!fs::exists(target / "CMakeCache.txt") && pgr_process::available("ninja")) {
        configure.push_back("-G");
        configure.push_back("Ninja");
    }
    // Toolchain-file handling is generated before project() by cmake_writer.
    if (builder.config().toolchain_file.empty())
        pgr_toolchain::cmake_compilers(builder.config().compiler, configure);
    else if (native_cmake)
        configure.push_back("-DCMAKE_TOOLCHAIN_FILE=" + pgr_workspace::utf8(
            fs::absolute(fs::u8path(builder.config().toolchain_file))));
    auto build = std::vector<std::string>{"cmake", "--build", pgr_workspace::utf8(target)};
    std::cout << "CI: " << pgr_process::display(configure) << '\n' << std::flush;
    if (pgr_process::run(configure) != 0) return 2;
    std::cout << "CI: " << pgr_process::display(build) << '\n' << std::flush;
    if (pgr_process::run(build) != 0) return 3;
    std::error_code ec;
    const auto commands = target / "compile_commands.json";
    if (fs::exists(commands)) {
        fs::copy_file(commands, project.store_root / "compile_commands.json",
            fs::copy_options::overwrite_existing, ec);
        if (ec) std::cerr << "CI: warning: compile_commands: " << ec.message() << '\n';
    }
    return 0;
}

static void print_help(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "  (no args)                 Select/create project in TUI\n"
              << "  --list                    Show registered projects\n"
              << "  --toolchains              Show compilers found in Pack\n"
              << "  --native-cmake            Use existing project CMakeLists.txt (with --ci)\n"
              << "  --create NAME --source-dir DIR [--starter]\n"
              << "                            Create project entry; optionally starter main.cpp\n"
              << "  --project NAME            Open selected project TUI\n"
              << "  --project NAME --ci       Build selected project without TUI\n"
              << "  --profile NAME            Named build profile (with --ci)\n"
              << "  --build-dir DIR           Build directory within source project\n"
              << "  --clean                   Clean build directory (only with --ci)\n"
              << "  --home DIR                Override executable directory (testing/portable)\n"
              << "  --help                    This help\n";
}

static bool select_project(const registry_t& registry, project_t& selected) {
    for (;;) {
        auto projects = registry.list();
        std::cout << "\n=== PGR_Make | Менеджер проектов ===\n"
                  << "Реестр: " << pgr_workspace::utf8(registry.projects_root()) << '\n'
                  << "Инструменты: " << pgr_workspace::utf8(registry.pack_root()) << '\n';
        if (projects.empty()) std::cout << "  (Сохранённых проектов нет)\n";
        for (std::size_t i = 0; i < projects.size(); ++i)
            std::cout << " " << (i + 1) << ". " << projects[i].name
                      << "  [" << pgr_workspace::utf8(projects[i].source_root) << "]\n";
        std::cout << " N. Создать новый проект\n R. Обновить список\n 0. Выход\nВыбор: " << std::flush;
        std::string input;
        if (!std::getline(std::cin, input) || input == "0" || input == "q") return false;
        if (input == "r" || input == "R") continue;
        if (input == "n" || input == "N") {
            std::string name, directory, error;
            std::cout << "Имя проекта: ";
            if (!std::getline(std::cin, name)) return false;
            std::cout << "Директория исходников (полный путь): ";
            if (!std::getline(std::cin, directory)) return false;
            std::cout << "Создать пример main.cpp, если его нет? [y/N]: ";
            std::string answer;
            if (!std::getline(std::cin, answer)) return false;
            if (create_project(registry, name, pgr_workspace::from_utf8(directory),
                               answer == "y" || answer == "Y" || answer == "д" || answer == "Д", selected, error)) {
                std::cout << "Проект создан: " << selected.name << '\n';
                return true;
            }
            std::cerr << "Ошибка: " << error << '\n';
            continue;
        }
        try {
            std::size_t pos = 0;
            auto n = std::stoul(input, &pos);
            if (pos == input.size() && n >= 1 && n <= projects.size()) {
                selected = projects[n - 1];
                return true;
            }
        } catch (...) {}
        std::cerr << "Неверный выбор\n";
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    // MSVCRT main(argv) may decode Unicode arguments in a non-UTF-8 codepage.
    // Read the Unicode Windows command line and convert every argument to UTF-8.
    std::vector<std::string> cli_args;
#ifdef _WIN32
    int wide_count = 0;
    LPWSTR* wide_args = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    if (!wide_args) { std::cerr << "Windows command line decode failed\n"; return 1; }
    for (int i = 0; i < wide_count; ++i)
        cli_args.push_back(pgr_workspace::utf8(fs::path(wide_args[i])));
    LocalFree(wide_args);
#else
    for (int i = 0; i < argc; ++i) cli_args.emplace_back(argv[i]);
#endif
    bool ci = false, clean = false, list = false, starter = false, toolchains = false, native_cmake = false;
    std::string project_name, create_name, profile, build_dir;
    fs::path source_dir, home = pgr_workspace::executable_directory();
    for (std::size_t i = 1; i < cli_args.size(); ++i) {
        const std::string a = cli_args[i];
        if (a == "--help" || a == "-h") { print_help(cli_args.front().c_str()); return 0; }
        else if (a == "--ci") ci = true;
        else if (a == "--clean") clean = true;
        else if (a == "--list") list = true;
        else if (a == "--toolchains") toolchains = true;
        else if (a == "--native-cmake") native_cmake = true;
        else if (a == "--starter") starter = true;
        else if ((a == "--project" || a == "--create" || a == "--source-dir" ||
                  a == "--profile" || a == "--build-dir" || a == "--home") && i + 1 < cli_args.size()) {
            const std::string value = cli_args[++i];
            if (a == "--project") project_name = value;
            else if (a == "--create") create_name = value;
            else if (a == "--source-dir") source_dir = pgr_workspace::from_utf8(value);
            else if (a == "--profile") profile = value;
            else if (a == "--build-dir") build_dir = value;
            else home = pgr_workspace::from_utf8(value);
        } else { std::cerr << "Unknown or incomplete argument: " << a << '\n'; return 1; }
    }
    if (clean && !ci) { std::cerr << "--clean requires --ci\n"; return 1; }
    if (ci && project_name.empty()) { std::cerr << "--ci requires --project NAME\n"; return 1; }
    if ((!create_name.empty() && (source_dir.empty() || ci)) ||
        (create_name.empty() && (!source_dir.empty() || starter))) {
        std::cerr << "--create NAME requires --source-dir DIR; --starter only with --create\n";
        return 1;
    }
    if (!home.is_absolute()) home = fs::absolute(home);
    registry_t registry(home);
    std::string error;
    if (!registry.ensure(error)) { std::cerr << "Workspace: " << error << '\n'; return 1; }
    pgr_pack::activate(registry.pack_root());

    if (toolchains) {
        for (const auto& path : pgr_pack::compilers(registry.pack_root()))
            std::cout << pgr_workspace::utf8(path) << "\t" << pgr_pack::version(path) << '\n';
        return 0;
    }
    if (list) {
        for (const auto& p : registry.list())
            std::cout << p.name << "\t" << pgr_workspace::utf8(p.source_root) << '\n';
        return 0;
    }
    if (!create_name.empty()) {
        project_t created;
        if (!create_project(registry, create_name, source_dir, starter, created, error)) {
            std::cerr << "Create: " << error << '\n'; return 1;
        }
        std::cout << "Registered " << created.name << " -> " << pgr_workspace::utf8(created.source_root)
                  << "\nConfig: " << pgr_workspace::utf8(created.config()) << '\n';
        return 0;
    }
    for (;;) {
        project_t selected;
        if (!project_name.empty()) {
            if (!registry.read(project_name, selected, error)) {
                std::cerr << "Open: " << error << '\n'; return 1;
            }
        } else if (!select_project(registry, selected)) return 0;
        std::error_code ec;
        fs::current_path(selected.source_root, ec);
        if (ec) { std::cerr << "Cannot access project: " << ec.message() << '\n'; return 1; }
        if (ci) return run_ci(selected, profile, build_dir, clean, native_cmake);
        tui_t tui(selected);
        const bool switch_project = tui.run();
        if (!switch_project) return 0;
        project_name.clear();
        fs::current_path(registry.root(), ec);
    }
}
