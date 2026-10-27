#ifndef PGR_GUI_LAYOUT_SETTINGS_HPP
#define PGR_GUI_LAYOUT_SETTINGS_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace pgr_gui_layout {
namespace fs = std::filesystem;

// UI-only preferences: deliberately not stored in individual projects' config.cfg.
// All values are bounded so a damaged preference file cannot hide the workspace.
struct settings {
    int window_width = 1400;
    int window_height = 900;
    int projects_split = 1010;
    int projects_permille = 720; // usable width of configuration area / 1000
    int output_split = 565;
    int output_permille = 650;
    int output_page = 0; // 0 result, 1 log, 2 diagnostic, 3 history
    int category = 1; // Start with essentials
    bool output_visible = true;
    bool autoscroll = true;
};

inline settings read(const fs::path& file) {
    settings out;
    std::ifstream in(file);
    std::string line;
    bool schema07 = false, saw_page = false, saw_projects_ratio = false, saw_output_ratio = false;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        int n = 0;
        try { n = std::stoi(value); } catch (...) { continue; }
        if (key == "schema") schema07 = n >= 7;
        else if (key == "window_width") out.window_width = std::clamp(n, 940, 3840);
        else if (key == "window_height") out.window_height = std::clamp(n, 620, 2160);
        else if (key == "projects_split") out.projects_split = std::clamp(n, 600, 3200);
        else if (key == "output_split") out.output_split = std::clamp(n, 280, 1900);
        else if (key == "output_page") {out.output_page = std::clamp(n, 0, 3);saw_page=true;}
        else if (key == "projects_permille") {out.projects_permille = std::clamp(n, 450, 850);saw_projects_ratio=true;}
        else if (key == "output_permille") {out.output_permille = std::clamp(n, 350, 850);saw_output_ratio=true;}
        else if (key == "category") out.category = std::clamp(n, 0, 6);
        else if (key == "output_visible") out.output_visible = n != 0;
        else if (key == "autoscroll") out.autoscroll = n != 0;
    }
    if (!schema07) {
        // v0.6 pages: log=0, diagnostics=1, history=2.
        // v0.7 inserts Result at page 0; retain the legacy page.
        if (saw_page) out.output_page = std::clamp(out.output_page+1,0,3);
        if (!saw_projects_ratio) out.projects_permille = std::clamp(
            out.projects_split * 1000 / std::max(1,out.window_width),450,850);
        if (!saw_output_ratio) out.output_permille = std::clamp(
            out.output_split * 1000 / std::max(1,out.window_height),350,850);
    }
    return out;
}

inline bool write(const fs::path& file, const settings& p) {
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;
    out << "schema=7\n" << "window_width=" << p.window_width << '\n'
        << "window_height=" << p.window_height << '\n'
        << "projects_split=" << p.projects_split << '\n'
        << "projects_permille=" << p.projects_permille << '\n'
        << "output_split=" << p.output_split << '\n'
        << "output_permille=" << p.output_permille << '\n'
        << "output_page=" << p.output_page << '\n'
        << "category=" << p.category << '\n'
        << "output_visible=" << (p.output_visible ? 1 : 0) << '\n'
        << "autoscroll=" << (p.autoscroll ? 1 : 0) << '\n';
    return static_cast<bool>(out);
}
} // namespace pgr_gui_layout
#endif
