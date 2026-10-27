#ifndef PGR_ANALYSIS_HPP
#define PGR_ANALYSIS_HPP
#include <filesystem>
#include <iostream>
#include <string>
#include "diagnostics.hpp"
#include "process_runner.hpp"
#include "workspace.hpp"

namespace pgr_analysis {
namespace fs = std::filesystem;
// clangd --check is a one-shot static check. This is NOT a full LSP client.
inline int clangd_check(const pgr_workspace::project_t& project,
                        pgr_diagnostics::collector& diagnostics,
                        const std::function<void(const std::string&)>& output={}) {
    if (!pgr_process::available("clangd")) {
        std::cerr << "clangd is not available in Pack/PATH\n";
        return 2;
    }
    const fs::path commands = project.build()/"compile_commands.json";
    if (!fs::is_regular_file(commands)) {
        std::cerr << "Compile first with CMake to generate compile_commands.json: "
                  << pgr_workspace::utf8(commands) << '\n';
        return 2;
    }
    const auto sources=pgr_workspace::discover_sources(project.source_root);
    if (sources.empty()) return 2;
    int failed=0;
    for (const auto& relative:sources) {
        const auto source=pgr_workspace::utf8(project.source_root/fs::u8path(relative));
        std::vector<std::string> command={"clangd", "--compile-commands-dir="+pgr_workspace::utf8(project.build()),
                                              "--check="+source};
        if (output) output("[clangd] " + pgr_process::display(command));
        const int rc=pgr_process::run(command,true,[&](const std::string& line){
            diagnostics.add_line(line,source);
            if (output) output(line);
        });
        if (rc!=0) ++failed;
    }
    diagnostics.save(project.store_root/"diagnostics.json");
    return failed ? 1 : 0;
}
}
#endif
