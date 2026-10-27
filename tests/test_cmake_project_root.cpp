#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "core/cmake_session.hpp"

namespace fs = std::filesystem;

static std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void write_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
}
static void require(bool success, const std::string& reason) {
    if (!success) throw std::runtime_error(reason);
}

int main(int argc, char** argv) {
    // Keep this test's data inside its CTest build tree, never in a user project.
    if (argc != 2) return 2;
    const fs::path original_cwd = fs::current_path();
    try {
        const fs::path sandbox = fs::absolute(fs::u8path(argv[1]));
        const fs::path source = sandbox / "Project Sources with spaces";
        const fs::path registry = sandbox / "Install" / "Project" / "Example";
        const fs::path application_cwd = sandbox / "Install";
        fs::create_directories(source / "lib");
        fs::create_directories(source / "vendor");
        fs::create_directories(registry);
        write_file(registry / "project.pgr", "test owner\n");
        write_file(source / "src" / "main.cpp",
                   "#include \"external.hpp\"\nint main(){return answer()-42;}\n");
        write_file(source / "gui" / "main.cpp", "int gui_function(){return 0;}\n");
        write_file(source / "tests" / "test.cpp", "int test_function(){return 0;}\n");
        write_file(source / "include" / "external.hpp", "inline int answer(){return 42;}\n");
        fs::current_path(application_cwd); // Mirrors GUI running from C:/PGR.
        pgr_workspace::project_t project{"Example", source, registry};
        make_t::config_t config;
        config.compiler = "g++";
        config.standard = "-std=c++17";
        config.source_files = {"src/main.cpp", "gui/main.cpp", "tests/test.cpp"};
        config.include_dirs = {"include"};
        config.library_dirs = {"lib"};
        config.package_dirs = {"vendor"};
        config.output_file = "dist/Example";
        std::string error;
        require(pgr_cmake::generate(project, config, error), "PGR CMake generation failed: " + error);
        const std::string all = read_file(project.stage() / "CMakeLists.txt") +
            read_file(project.stage() / "src" / "CMakeLists.txt") +
            read_file(project.stage() / "gui" / "CMakeLists.txt") +
            read_file(project.stage() / "tests" / "CMakeLists.txt");
        for (const auto& name : {"src/main.cpp", "gui/main.cpp", "tests/test.cpp",
                                  "include", "lib", "vendor", "dist"}) {
            const std::string expected = (source / fs::u8path(name)).generic_u8string();
            require(all.find(expected) != std::string::npos,
                    "CMake references wrong root for " + std::string(name) + ": " + expected);
        }
        const std::string wrong = (application_cwd / "src" / "main.cpp").generic_u8string();
        require(all.find(wrong) == std::string::npos, "CMake leaked GUI working directory");
        fs::current_path(original_cwd);
        std::cout << "PASS: CMake paths are rooted in the selected project, not GUI cwd\n";
    } catch (const std::exception& e) {
        fs::current_path(original_cwd);
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
