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
    int output_split = 565;
    int output_page = 0;
    int category = 0;
    bool output_visible = true;
    bool autoscroll = true;
};

inline settings read(const fs::path& file) {
    settings out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        int n = 0;
        try { n = std::stoi(value); } catch (...) { continue; }
        if (key == "window_width") out.window_width = std::clamp(n, 940, 3840);
        else if (key == "window_height") out.window_height = std::clamp(n, 620, 2160);
        else if (key == "projects_split") out.projects_split = std::clamp(n, 600, 3200);
        else if (key == "output_split") out.output_split = std::clamp(n, 280, 1900);
        else if (key == "output_page") out.output_page = std::clamp(n, 0, 2);
        else if (key == "category") out.category = std::clamp(n, 0, 6);
        else if (key == "output_visible") out.output_visible = n != 0;
        else if (key == "autoscroll") out.autoscroll = n != 0;
    }
    return out;
}

inline bool write(const fs::path& file, const settings& p) {
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;
    out << "window_width=" << p.window_width << '\n'
        << "window_height=" << p.window_height << '\n'
        << "projects_split=" << p.projects_split << '\n'
        << "output_split=" << p.output_split << '\n'
        << "output_page=" << p.output_page << '\n'
        << "category=" << p.category << '\n'
        << "output_visible=" << (p.output_visible ? 1 : 0) << '\n'
        << "autoscroll=" << (p.autoscroll ? 1 : 0) << '\n';
    return static_cast<bool>(out);
}
} // namespace pgr_gui_layout
#endif
