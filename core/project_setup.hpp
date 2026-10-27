#ifndef PGR_PROJECT_SETUP_HPP
#define PGR_PROJECT_SETUP_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include "make.hpp"
#include "workspace.hpp"

// Shared by the CLI and GTK interface. Creating a project never overwrites
// an existing main.cpp; metadata stays in <PGR home>/Project/<name>.
namespace pgr_project {
namespace fs = std::filesystem;

inline bool configure_new(const pgr_workspace::project_t& project, std::string& error) {
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
    if (!cpp && !sources.empty()) {
        builder.set_compiler("gcc");
        builder.set_c_standard("-std=c11");
    }
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

inline bool create_project(const pgr_workspace::registry_t& registry,
                           const std::string& name, const fs::path& source,
                           bool starter, pgr_workspace::project_t& result,
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
} // namespace pgr_project
#endif
