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
#include "core/diagnostics.hpp"
#include "core/analysis.hpp"
#include "core/presets.hpp"
#include "core/artifacts.hpp"
#include "core/make.hpp"
#include "core/pack_tools.hpp"
#include "core/process_runner.hpp"
#include "core/safe_paths.hpp"
#include "core/tree.hpp"
#include "core/toolchain.hpp"
#include "core/cmake_session.hpp"
#include "core/workspace.hpp"
#include "core/project_setup.hpp"
#include "core/build_controller.hpp"
#include "core/pack_manager.hpp"
#include "core/pack_bulk.hpp"

namespace fs = std::filesystem;
using pgr_workspace::project_t;
using pgr_workspace::registry_t;

static void print_help(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "  (no args)                 Select/create project in TUI\n"
              << "  --list                    Show registered projects\n"
              << "  --toolchains              Show compilers found in Pack (GCC/Clang/MSVC)\n"
              << "  --pack-list               List registered installed tool versions\n"
              << "  --pack-catalog            List available vetted packages\n"
              << "  --pack-install ID         Install catalog package from HTTPS (SHA-256 required)\n"
              << "  --pack-install-all        Download newest verified GCC, Clang, CMake, Ninja into Pack\n"
              << "  --pack-import ID ZIP      Install catalog package from local ZIP\n"
              << "  --pack-import-custom ID ZIP SHA256 EXE  Import a verified ZIP outside catalog\n"
              << "  --pack-register ID DIR EXE Register external tool installation\n"
              << "  --pack-bind SLOT ID       Bind project to compiler/cmake/ninja/objcopy version\n"
              << "  --pack-unbind SLOT        Remove project's version pin\n"
              << "  --pack-retire ID          Unregister and quarantine unused version\n"
              << "  --diagnostics             Print last structured diagnostics JSON\n"
              << "  --analyze                 Run clangd --check (after CMake build)\n"
              << "  --write-presets           Generate isolated CMakePresets.json\n"
              << "  --generate-only           Generate CMake without building\n"
              << "  --clean-only              Clean owned build directory without rebuilding\n"
              << "  --show-command            Show direct compile command\n"
              << "  --export-commands         Export compile_commands.json from owned build\n"
              << "  --native-cmake            Use existing project CMakeLists.txt (with --ci)\n"
              << "  --direct                  Direct compiler build (with --ci)\n"
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
            if (pgr_project::create_project(registry, name, pgr_workspace::from_utf8(directory),
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
    (void)argc;
    (void)argv;
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
    bool analyze = false, diagnostics = false, write_presets = false, direct = false;
    bool generate_only = false, clean_only = false, show_command = false, export_commands = false;
    std::string project_name, create_name, profile, build_dir;
    std::string pack_action, pack_id, pack_slot, pack_value;
    fs::path pack_archive, pack_root;
    std::string pack_exe;
    fs::path source_dir, home = pgr_workspace::executable_directory();
    for (std::size_t i = 1; i < cli_args.size(); ++i) {
        const std::string a = cli_args[i];
        if (a == "--help" || a == "-h") { print_help(cli_args.front().c_str()); return 0; }
        else if (a == "--ci") ci = true;
        else if (a == "--clean") clean = true;
        else if (a == "--list") list = true;
        else if (a == "--toolchains") toolchains = true;
        else if (a == "--pack-list" || a == "--pack-catalog" || a == "--pack-install-all") pack_action=a;
        else if ((a == "--pack-install" || a == "--pack-retire") && i+1<cli_args.size()) {
            pack_action=a;pack_id=cli_args[++i];
        }
        else if (a=="--pack-import" && i+2<cli_args.size()) {
            pack_action=a;pack_id=cli_args[++i];pack_archive=pgr_workspace::from_utf8(cli_args[++i]);
        }
        else if (a=="--pack-import-custom" && i+4<cli_args.size()) {
            pack_action=a;pack_id=cli_args[++i];pack_archive=pgr_workspace::from_utf8(cli_args[++i]);
            pack_value=cli_args[++i];pack_exe=cli_args[++i];
        }
        else if (a=="--pack-register" && i+3<cli_args.size()) {
            pack_action=a;pack_id=cli_args[++i];pack_root=pgr_workspace::from_utf8(cli_args[++i]);pack_exe=cli_args[++i];
        }
        else if (a=="--pack-bind" && i+2<cli_args.size()) {
            pack_action=a;pack_slot=cli_args[++i];pack_value=cli_args[++i];
        }
        else if (a=="--pack-unbind" && i+1<cli_args.size()) {
            pack_action=a;pack_slot=cli_args[++i];
        }
        else if (a == "--analyze") analyze = true;
        else if (a == "--diagnostics") diagnostics = true;
        else if (a == "--write-presets") write_presets = true;
        else if (a == "--generate-only") generate_only = true;
        else if (a == "--clean-only") clean_only = true;
        else if (a == "--show-command") show_command = true;
        else if (a == "--export-commands") export_commands = true;
        else if (a == "--native-cmake") native_cmake = true;
        else if (a == "--direct") direct = true;
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
    if(!pack_action.empty() && (ci || analyze || diagnostics || write_presets || generate_only || clean_only || show_command || export_commands ||
                                !create_name.empty() || list || toolchains)) {
        std::cerr << "Pack command cannot be combined with build command\n"; return 1;
    }
    if (direct && (!ci || native_cmake || clean)) {
        std::cerr << "--direct requires --ci and cannot be combined with --native-cmake or --clean\n";
        return 1;
    }
    if (clean && !ci) { std::cerr << "--clean requires --ci\n"; return 1; }
    if ((ci || analyze || diagnostics || write_presets || generate_only || clean_only || show_command || export_commands) && project_name.empty()) {
        std::cerr << "Project action requires --project NAME\n"; return 1;
    }
    if ((static_cast<int>(ci)+analyze+diagnostics+write_presets+generate_only+clean_only+show_command+export_commands)>1) {
        std::cerr << "Choose one project action per invocation\n"; return 1;
    }
    if ((!create_name.empty() && (source_dir.empty() || ci)) ||
        (create_name.empty() && (!source_dir.empty() || starter))) {
        std::cerr << "--create NAME requires --source-dir DIR; --starter only with --create\n";
        return 1;
    }
    if (!home.is_absolute()) home = fs::absolute(home);
    registry_t registry(home);
    std::string error;
    if (!registry.ensure(error)) { std::cerr << "Workspace: " << error << '\n'; return 1; }
    // Legacy TUI and utility modes still use PATH-based discovery.
    // CI uses explicit per-project tools from BuildController instead.
    if (!ci && pack_action.empty()) pgr_pack::activate(registry.pack_root());

    if (!pack_action.empty()) {
        pgr_pack_manager::manager manager(registry.pack_root(),registry.root());
        if(!manager.load(error)) {std::cerr<<"Pack: "<<error<<'\n';return 1;}
        if(pack_action=="--pack-list") {
            for(const auto& x:manager.entries()) {
                std::string diagnostic;
                const bool ok=pgr_pack_manager::verify_tool(x,diagnostic);
                std::cout<<x.id<<'\t'<<(ok?"ready":"invalid")<<'\t'<<pgr_workspace::utf8(pgr_pack_manager::executable_path(x))<<'\n';
            }
            return 0;
        }
        auto list=pgr_pack_bulk::merged_catalog(registry.root(),registry.pack_root(),error);
        if(pack_action=="--pack-catalog") {
            if(!error.empty()) {std::cerr<<error<<'\n';return 1;}
            for(const auto& x:list) std::cout<<x.id<<'\t'<<x.url<<'\n';
            return 0;
        }
        if(pack_action=="--pack-install-all") {
            if(!error.empty()) {std::cerr<<"Pack catalog: "<<error<<'\n';return 1;}
            const auto plan=pgr_pack_bulk::latest(list,manager);
            for(const auto& id:plan.already_installed) std::cout<<"[Pack] already installed: "<<id<<'\n';
            for(const auto& issue:plan.unavailable) std::cerr<<"[Pack] missing catalog entry: "<<issue<<'\n';
            for(const auto& issue:plan.invalid) std::cerr<<"[Pack] invalid installed tool: "<<issue<<'\n';
            if(!plan.ready()) {std::cerr<<"[Pack] No downloads started: the required toolchain cannot be completed safely.\n";return 1;}
            if(plan.downloads.empty()) {std::cout<<"[Pack] All required verified versions are installed.\n";return 0;}
            unsigned complete=0;
            for(const auto& pkg:plan.downloads) {
                std::cout<<"[Pack] Downloading "<<pkg.id<<" ("<<(complete+1)<<"/"<<plan.downloads.size()<<")\n"<<std::flush;
                std::string install_error;
                if(!manager.install(pkg,{},install_error)) {
                    std::cerr<<"[Pack] Failed "<<pkg.id<<": "<<install_error<<'\n';
                    // Keep verified earlier installations. Retry will skip them.
                    return 1;
                }
                ++complete;
                std::cout<<"[Pack] Installed "<<pkg.id;
                for(const auto& companion:pkg.provides) std::cout<<", "<<companion.tool<<"@"<<companion.version;
                std::cout<<'\n'<<std::flush;
            }
            std::cout<<"[Pack] Ready: GCC, Clang, CMake and Ninja.\n";
            return 0;
        }
        auto bind=[&](bool unbind)->int {
            if(project_name.empty()) {std::cerr<<"--pack-bind requires --project NAME\n";return 1;}
            if(pack_slot!="compiler" && pack_slot!="cmake" && pack_slot!="ninja" && pack_slot!="objcopy") {
                std::cerr<<"Unsupported pin slot\n";return 1;
            }
            pgr_workspace::project_t p;
            if(!registry.read(project_name,p,error)) {std::cerr<<error<<'\n';return 1;}
            make_t config;
            if(config.load(pgr_workspace::utf8(p.config()))!=0) return 1;
            if(!unbind) {
                const auto* entry=manager.find(pack_value);
                if(!entry || (pack_slot=="compiler"?!(entry->tool=="gcc"||entry->tool=="clang"||entry->tool=="msvc"):entry->tool!=pack_slot) ||
                   !pgr_pack_manager::verify_tool(*entry,error)) {std::cerr<<"Cannot bind: "<<error<<'\n';return 1;}
                if(pack_slot=="compiler")config.set_compiler(pgr_workspace::utf8(pgr_pack_manager::executable_path(*entry)));
            }
            const auto& c=config.config();
            if(unbind && pack_slot=="compiler" && !c.compiler_id.empty()) {
                // Otherwise an unbound project keeps pointing to a retired absolute path.
                const auto file=fs::u8path(c.compiler).filename().u8string();
                if(file=="g++"||file=="g++.exe"||file=="clang++"||file=="clang++.exe"||file=="cl.exe"||file=="cl")
                    config.set_compiler(file);
            }
            const std::string value=unbind?"":pack_value;
            config.set_pins(pack_slot=="compiler"?value:c.compiler_id,pack_slot=="cmake"?value:c.cmake_id,
                            pack_slot=="ninja"?value:c.ninja_id,pack_slot=="objcopy"?value:c.objcopy_id);
            if(config.save(pgr_workspace::utf8(p.config()))!=0) return 1;
            std::cout<<"Pin updated: "<<pack_slot<<" = "<<value<<'\n';return 0;
        };
        if(pack_action=="--pack-bind"||pack_action=="--pack-unbind")return bind(pack_action=="--pack-unbind");
        if(pack_action=="--pack-retire") {
            std::vector<std::string> references;
            for(const auto& entry:registry.entries()) {
                // Include projects whose source drive is currently disconnected.
                const fs::path store=registry.projects_root()/pgr_workspace::from_utf8(entry.name);
                std::vector<fs::path> profiles={store/"config.cfg"};
                std::error_code ec;
                for(fs::directory_iterator it(store/"profiles",fs::directory_options::skip_permission_denied,ec),end;
                    !ec && it!=end;it.increment(ec)) if(it->is_regular_file()&&it->path().extension()==".cfg") profiles.push_back(it->path());
                for(const auto& path:profiles) {
                    make_t cfg;if(cfg.load(pgr_workspace::utf8(path))!=0)continue;
                    const auto& c=cfg.config();
                    for(const auto& id:{c.compiler_id,c.cmake_id,c.ninja_id,c.objcopy_id})if(!id.empty())references.push_back(id);
                }
            }
            if(!manager.remove(pack_id,references,error)){std::cerr<<"Retire: "<<error<<'\n';return 1;}
            std::cout<<"Retired tool metadata; managed installation moved to Pack/Retired\n";return 0;
        }
        if(pack_action=="--pack-register") {
            // Format tool@version@platform is the identity of an externally installed bundle.
            const auto first=pack_id.find('@'),second=pack_id.find('@',first==std::string::npos?0:first+1);
            if(first==std::string::npos||second==std::string::npos) {std::cerr<<"Expected TOOL@VERSION@PLATFORM\n";return 1;}
            pgr_pack_manager::installed x;
            x.id=pack_id;x.tool=pack_id.substr(0,first);x.version=pack_id.substr(first+1,second-first-1);
            x.platform=pack_id.substr(second+1);x.root=fs::absolute(pack_root);
            x.executable=pack_exe;
            if(x.platform!=pgr_pack_manager::platform()||!manager.register_external(x,error)) {
                std::cerr<<"Register: "<<error<<'\n';return 1;
            }
            std::cout<<"Registered "<<x.id<<'\n';return 0;
        }
        if(pack_action=="--pack-import-custom") {
            const auto first=pack_id.find('@'),second=pack_id.find('@',first==std::string::npos?0:first+1);
            if(first==std::string::npos||second==std::string::npos) {std::cerr<<"Expected TOOL@VERSION@PLATFORM\n";return 1;}
            pgr_pack_manager::package item;
            item.id=pack_id;item.tool=pack_id.substr(0,first);item.version=pack_id.substr(first+1,second-first-1);
            item.platform=pack_id.substr(second+1);item.sha256=pack_value;item.executable=pack_exe;
            if(!manager.install(item,pack_archive,error)){std::cerr<<"Import: "<<error<<'\n';return 1;}
            std::cout<<"Imported "<<item.id<<'\n';return 0;
        }
        if(!error.empty()) {std::cerr<<error<<'\n';return 1;}
        auto it=std::find_if(list.begin(),list.end(),[&](const auto& x){return x.id==pack_id;});
        if(it==list.end()) {std::cerr<<"Unknown catalog package: "<<pack_id<<'\n';return 1;}
        const fs::path archive=pack_action=="--pack-import"?pack_archive:fs::path{};
        if(!manager.install(*it,archive,error)) {std::cerr<<"Install: "<<error<<'\n';return 1;}
        std::cout<<"Installed "<<pack_id<<'\n';return 0;
    }
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
        if (!pgr_project::create_project(registry, create_name, source_dir, starter, created, error)) {
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
        if (generate_only) {
            make_t config;
            if (config.load(pgr_workspace::utf8(selected.config())) != 0) return 1;
            if (!pgr_cmake::generate(selected, config.config(), error)) {
                std::cerr << "Generate: " << error << '\n'; return 1;
            }
            std::cout << "Generated " << pgr_workspace::utf8(selected.stage()) << '\n';
            return 0;
        }
        if (clean_only) {
            make_t settings;
            if(settings.load(pgr_workspace::utf8(selected.config()))!=0) return 1;
            const auto target = build_dir.empty() ? pgr_build::default_build_directory(selected,settings.config()) :
                pgr_paths::absolute_build_dir(fs::u8path(build_dir), selected.source_root);
            if (!pgr_paths::clean(target, selected.source_root, selected.store_root, error)) {
                std::cerr << "Clean: " << error << '\n'; return 1;
            }
            std::cout << "Cleaned " << pgr_workspace::utf8(target) << '\n'; return 0;
        }
        if (export_commands) {
            make_t settings;
            if(settings.load(pgr_workspace::utf8(selected.config()))!=0) return 1;
            const auto target = build_dir.empty() ? pgr_build::default_build_directory(selected,settings.config()) :
                pgr_paths::absolute_build_dir(fs::u8path(build_dir), selected.source_root);
            if (!pgr_paths::safe_build_dir(target, selected.source_root)) {
                std::cerr << "Export: unsafe build directory\n"; return 1;
            }
            const auto source = target / "compile_commands.json";
            const auto destination = selected.source_root / "compile_commands.json";
            if (!fs::is_regular_file(source)) {
                std::cerr << "Export: run a CMake build first\n"; return 1;
            }
            if (fs::exists(destination)) {
                std::cerr << "Export: refusing to overwrite existing " << pgr_workspace::utf8(destination) << '\n';
                return 1;
            }
            std::error_code ec_copy;
            fs::copy_file(source, destination, fs::copy_options::none, ec_copy);
            if (ec_copy) { std::cerr << "Export: " << ec_copy.message() << '\n'; return 1; }
            std::cout << "Exported to " << pgr_workspace::utf8(destination) << '\n';
            return 0;
        }
        if (show_command) {
            make_t config;
            if (config.load(pgr_workspace::utf8(selected.config())) != 0 ||
                config.build_args() != 0) {
                std::cerr << "Command: cannot prepare direct command\n"; return 1;
            }
            std::cout << pgr_process::display(config.args()) << '\n'; return 0;
        }
        if (ci) {
            pgr_build::options params;
            params.profile=profile; params.build_dir=build_dir;
            params.clean=clean; params.native_cmake=native_cmake; params.direct=direct;
            params.pack=registry.pack_root();
            return pgr_build::run(selected,params);
        }
        if (diagnostics) {
            std::ifstream f(selected.store_root/"diagnostics.json", std::ios::binary);
            if (!f) { std::cerr << "No diagnostics report yet\n"; return 1; }
            std::cout << f.rdbuf(); return 0;
        }
        if (analyze) {
            pgr_diagnostics::collector found;
            const int rc=pgr_analysis::clangd_check(selected,found,
                [](const std::string& l){ std::cout << l << '\n'; });
            found.print(); return rc;
        }
        if (write_presets) {
            make_t cfg;
            if (cfg.load(pgr_workspace::utf8(selected.config())) != 0) return 1;
            if (!pgr_cmake::generate(selected,cfg.config(),error) ||
                !pgr_presets::write(selected,cfg.config(),error)) {
                std::cerr << "Presets: " << error << '\n'; return 1;
            }
            std::cout << "Created " << pgr_workspace::utf8(selected.stage()/"CMakePresets.json") << '\n';
            return 0;
        }
        tui_t tui(selected);
        const bool switch_project = tui.run();
        if (!switch_project) return 0;
        project_name.clear();
        fs::current_path(registry.root(), ec);
    }
}
