#ifndef PGR_BUILD_CONTROLLER_HPP
#define PGR_BUILD_CONTROLLER_HPP
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <iostream>
#include <vector>
#include "workspace.hpp"
#include "make.hpp"
#include "diagnostics.hpp"
#include "cmake_writer.hpp"
#include "cmake_session.hpp"
#include "toolchain.hpp"
#include "toolchain_manager.hpp"
#include "pack_manager.hpp"
#include "presets.hpp"
#include "artifacts.hpp"
#include "safe_paths.hpp"
namespace pgr_build {
namespace fs=std::filesystem;
using pgr_workspace::project_t;
struct options {
    std::string profile, build_dir;
    fs::path pack;
    bool clean=false, native_cmake=false, direct=false;
};
struct hooks {
    pgr_process::cancel_token* cancel=nullptr;
    std::function<void(const std::string&)> on_output;
};
inline fs::path default_build_directory(const project_t& project,const make_t::config_t& cfg) {
    fs::path target=project.build();
    const std::string key=cfg.compiler_id+"|"+cfg.cmake_id+"|"+cfg.ninja_id+"|"+cfg.objcopy_id;
    if(key=="|||") return target;
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:key) {hash^=c;hash*=1099511628211ULL;}
    std::ostringstream suffix;suffix<<std::hex<<hash;
    return target/"environments"/suffix.str();
}
inline int execute_build(const project_t& project, const options& options, const hooks& hooks = {}) {
    const auto& profile = options.profile;
    const auto& build_dir = options.build_dir;
    const bool clean = options.clean, native_cmake = options.native_cmake, direct = options.direct;
    if (hooks.cancel && hooks.cancel->requested.load()) return 130;
    auto output = [&](const std::string& message) {
        if (hooks.on_output) hooks.on_output(message);
        else std::cout << message << '\n' << std::flush;
    };
    std::vector<fs::path> tool_bins;
    auto execute = [&](const std::vector<std::string>& cmd,
                       const std::function<void(const std::string&)>& capture) {
        pgr_process::options po;
        po.quiet = static_cast<bool>(hooks.on_output);
        po.cancel = hooks.cancel;
        po.working_directory = project.source_root;
        po.extra_path=tool_bins;
        po.output = [&](const std::string& line) {
            if (hooks.on_output) hooks.on_output(line);
            capture(line);
        };
        return pgr_process::run_ex(cmd, po);
    };
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
    // Strict version pins: never silently fall back to another tool.
    pgr_pack_manager::manager pack_manager(options.pack,options.pack.parent_path());
    std::string pin_error;
    if (!pack_manager.load(pin_error)) {
        std::cerr << "Pack: " << pin_error << '\n'; return 1;
    }
    const auto pinned=[&](const std::string& id,const std::string& expected)->fs::path {
        if(id.empty()) return {};
        const auto* entry=pack_manager.find(id);
        if(!entry||entry->tool!=expected||entry->platform!=pgr_pack_manager::platform()) {
            pin_error="missing or incompatible pinned tool: "+id;return {};
        }
        if(!pgr_pack_manager::verify_tool(*entry,pin_error)) return {};
        auto path=pgr_pack_manager::executable_path(*entry);
        tool_bins.push_back(path.parent_path());
        return path;
    };
    fs::path compiler_override;
    if(!builder.config().compiler_id.empty()) {
        const auto* entry=pack_manager.find(builder.config().compiler_id);
        if(entry && (entry->tool=="gcc"||entry->tool=="clang"||entry->tool=="msvc"))
            compiler_override=pinned(entry->id,entry->tool);
        else pin_error="unknown pinned compiler: "+builder.config().compiler_id;
    }
    if(!builder.config().compiler_id.empty() && compiler_override.empty()) {
        std::cerr << "Pack: " << pin_error << '\n';return 1;
    }
    const auto cmake_override=pinned(builder.config().cmake_id,"cmake");
    if(!builder.config().cmake_id.empty() && cmake_override.empty()) {
        std::cerr << "Pack: " << pin_error << '\n';return 1;
    }
    const auto ninja_override=pinned(builder.config().ninja_id,"ninja");
    if(!builder.config().ninja_id.empty() && ninja_override.empty()) {
        std::cerr << "Pack: " << pin_error << '\n';return 1;
    }
    const auto objcopy_override=pinned(builder.config().objcopy_id,"objcopy");
    if(!builder.config().objcopy_id.empty() && objcopy_override.empty()) {
        std::cerr << "Pack: " << pin_error << '\n';return 1;
    }
    if (!compiler_override.empty()) builder.set_compiler(pgr_workspace::utf8(compiler_override));
    if (!objcopy_override.empty()) builder.set_objcopy(pgr_workspace::utf8(objcopy_override));
    // Prefix only the selected toolchain's runtime DLL folders for the child process.
    output("[Pack] Compiler: "+ (compiler_override.empty()?"automatic":pgr_workspace::utf8(compiler_override)));
    output("[Pack] CMake: "+ (cmake_override.empty()?"automatic":pgr_workspace::utf8(cmake_override)));
    // Resolve project-specific tools without changing PATH for other projects.
    // The absolute compiler is then used by both DirectBuilder and CMake.
    const std::string wanted = pgr_toolchain::selected_compiler(
        builder.config().compiler,builder.config().toolchain_prefix);
    const fs::path selected_tool = !compiler_override.empty()?compiler_override:pgr_tools::find(options.pack,wanted);
    if (selected_tool.empty()) {
        std::cerr << "Build: compiler cannot be resolved: " << wanted << '\n';return 1;
    }
    if (!compiler_override.empty()) builder.set_compiler(pgr_workspace::utf8(compiler_override));
    else if (selected_tool.is_absolute()) builder.set_compiler(pgr_workspace::utf8(selected_tool));
    else if (hooks.on_output) hooks.on_output("[Toolchain] Из Pack не выбран конкретный компилятор; используется системный PATH: " + wanted);
    if (builder.validate_configuration() != 0) {
        std::cerr << "CI: invalid configuration\n";
        return 1;
    }
    // Distinct caches for distinct tool versions; no migration of old CMakeCache.
    const fs::path target = build_dir.empty() ? default_build_directory(project,builder.config()) :
        pgr_paths::absolute_build_dir(fs::u8path(build_dir), project.source_root);
    if (!pgr_paths::safe_build_dir(target, project.source_root)) {
        std::cerr << "CI: unsafe build directory: " << build_dir << '\n';
        return 1;
    }
    if (direct) {
        // Run the same direct backend as TUI, with isolated project logs and
        // structured diagnostics. This mode does not generate CMake files.
        if (clean) { std::cerr << "CI: --clean is only supported for CMake builds\n"; return 1; }
        if (builder.build_args() != 0) return 1;
        const fs::path output = fs::u8path(builder.config().output_file);
        if (output.empty() || output.is_absolute()) {
            std::cerr << "CI: direct output path must be project-relative\n"; return 1;
        }
        std::error_code ec;
        const fs::path absolute_output = fs::weakly_canonical(project.source_root / output, ec);
        const fs::path absolute_source = fs::weakly_canonical(project.source_root, ec);
        if (ec || absolute_output == absolute_source ||
            !pgr_paths::within(absolute_output, absolute_source)) {
            std::cerr << "CI: unsafe direct output path\n"; return 1;
        }
        if (output.begin() == output.end() || *output.begin() != fs::path("build") ||
            !pgr_paths::within(absolute_output, absolute_source / "build")) {
            std::cerr << "CI: direct output must stay under project/build\n"; return 1;
        }
        std::string ownership_error;
        if (!pgr_paths::prepare(project.source_root / "build", project.source_root,
                                project.store_root, ownership_error)) {
            std::cerr << "CI: direct output directory: " << ownership_error << '\n';
            return 1;
        }
        if (!output.parent_path().empty()) {
            fs::create_directories(output.parent_path(), ec);
            if (ec) { std::cerr << "CI: cannot create output directory\n"; return 1; }
        }
        pgr_diagnostics::collector diagnostics;
        std::ofstream log(project.log(), std::ios::app | std::ios::binary);
        const int result = builder.run([&](const std::string& line) {
            diagnostics.add_line(line);
            if (hooks.on_output) hooks.on_output(line);
            if (log) log << "[DIRECT] " << line << '\n';
        }, hooks.cancel, project.source_root);
        diagnostics.save(project.store_root / "diagnostics.json");
        if (hooks.cancel && hooks.cancel->requested.load()) return 130;
        if (result != 0) diagnostics.print(std::cerr);
        return result == 0 ? 0 : 3;
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

    const auto selected = fs::u8path(builder.config().compiler);
    const auto cmake_path = cmake_override.empty()?pgr_tools::find(options.pack, "cmake", selected):cmake_override;
    const auto ninja_path = ninja_override.empty()?pgr_tools::find(options.pack, "ninja", selected, false):ninja_override;
    std::vector<std::string> configure = {
        pgr_workspace::utf8(cmake_path), "-S", pgr_workspace::utf8(native_cmake ? project.source_root : project.stage()), "-B", pgr_workspace::utf8(target)
    };
    if (!fs::exists(target / "CMakeCache.txt") && !ninja_path.empty()) {
        configure.push_back("-G");
        configure.push_back("Ninja");
        configure.push_back("-DCMAKE_MAKE_PROGRAM=" + pgr_workspace::utf8(ninja_path));
    }
    // Toolchain-file handling is generated before project() by cmake_writer.
    pgr_toolchain::cmake_compilers(
        pgr_toolchain::selected_compiler(builder.config().compiler, builder.config().toolchain_prefix), configure);
    if (!pgr_presets::configure_toolchain(project, builder.config(), configure, prep_error)) {
        std::cerr << "CI: " << prep_error << '\n'; return 1;
    }
    configure.push_back("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON");
    auto build = std::vector<std::string>{pgr_workspace::utf8(cmake_path), "--build", pgr_workspace::utf8(target)};
    pgr_diagnostics::collector diagnostics;
    std::ofstream ci_log(project.log(), std::ios::app | std::ios::binary);
    const auto capture = [&](const std::string& line) {
        diagnostics.add_line(line);
        if (ci_log) ci_log << "[CI] " << line << '\n';
    };
    output("CI: " + pgr_process::display(configure));
    const int configure_rc=execute(configure,capture);
    if (configure_rc != 0) {
        diagnostics.save(project.store_root/"diagnostics.json"); return configure_rc==130?130:2;
    }
    output("CI: " + pgr_process::display(build));
    const int build_rc = execute(build, capture);
    diagnostics.save(project.store_root/"diagnostics.json");
    if (build_rc != 0) { if (build_rc == 130) return 130; diagnostics.print(std::cerr); return 3; }
    if (hooks.cancel && hooks.cancel->requested.load()) return 130;
    if (!native_cmake && !pgr_artifacts::export_elf(project,builder.config(),prep_error,capture,hooks.cancel)) {
        std::cerr << "CI post-build: " << prep_error << '\n'; return 4;
    }
    std::error_code ec;
    const auto commands = target / "compile_commands.json";
    if (fs::exists(commands)) {
        fs::copy_file(commands, project.store_root / "compile_commands.json",
            fs::copy_options::overwrite_existing, ec);
        if (ec) std::cerr << "CI: warning: compile_commands: " << ec.message() << '\n';
    }
    return 0;
}

inline int run(const project_t& project, const options& parameters, const hooks& callbacks = {}) {
    const auto start=std::chrono::system_clock::now();
    // A failed configure must not keep showing diagnostics from the previous build.
    pgr_diagnostics::collector{}.save(project.store_root/"diagnostics.json");
    const int result=execute_build(project,parameters,callbacks);
    std::error_code ec;
    const fs::path directory=project.store_root/"history";
    fs::create_directories(directory,ec);
    if (!ec) {
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now()-start).count();
        const std::string mode=parameters.direct?"direct":parameters.native_cmake?"native-cmake":"generated-cmake";
        std::ofstream index(project.store_root/"history.log",std::ios::binary|std::ios::app);
        if (index) index << ms << '\t' << result << '\t' << elapsed << '\t' << mode << '\n';
        const fs::path diagnostic=project.store_root/"diagnostics.json";
        if (fs::is_regular_file(diagnostic,ec)) {
            const auto snapshot=directory/(std::to_string(ms)+"_"+std::to_string(result)+".json");
            fs::copy_file(diagnostic,snapshot,fs::copy_options::none,ec);
        }
    }
    return result;
}

}
#endif
