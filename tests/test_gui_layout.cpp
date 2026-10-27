#include "../gui/layout_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    assert(argc == 2);
    fs::path file = fs::u8path(argv[1]);
    fs::create_directories(file.parent_path());
    auto initial = pgr_gui_layout::read(file);
    assert(initial.window_width >= 940);
    assert(initial.output_visible && initial.autoscroll);
    initial.window_width = 1650;
    initial.window_height = 970;
    initial.projects_split = 1120;
    initial.projects_permille = 700;
    initial.output_permille = 620;
    initial.output_split = 690;
    initial.output_page = 2;
    initial.category = 3;
    initial.output_visible = false;
    initial.autoscroll = false;
    assert(pgr_gui_layout::write(file, initial));
    auto restored = pgr_gui_layout::read(file);
    assert(restored.window_width == 1650);
    assert(restored.window_height == 970);
    assert(restored.projects_split == 1120);
    assert(restored.projects_permille == 700);
    assert(restored.output_permille == 620);
    assert(restored.output_split == 690);
    assert(restored.output_page == 2);
    assert(restored.category == 3);
    assert(!restored.output_visible && !restored.autoscroll);
    {
        std::ofstream out(file, std::ios::trunc);
        out << "output_page=999\ncategory=-100\nwindow_width=no-number\n"
            << "projects_split=-1\noutput_visible=1\n";
    }
    auto malformed = pgr_gui_layout::read(file);
    assert(malformed.output_page == 3);
    assert(malformed.category == 0);
    assert(malformed.window_width == 1400);
    assert(malformed.projects_split == 600);
    assert(malformed.output_visible);
    // Migration: v0.6 had Log=0. After adding Result at index 0, open Log=1.
    {
        std::ofstream old(file, std::ios::trunc);
        old << "output_page=0\nprojects_split=1050\nwindow_width=1500\n";
    }
    auto migrated = pgr_gui_layout::read(file);
    assert(migrated.output_page == 1);
    assert(migrated.projects_permille == 700);
    assert(pgr_gui_layout::write(file,migrated));
    auto reloaded = pgr_gui_layout::read(file);
    assert(reloaded.output_page == 1);
    assert(reloaded.projects_permille == 700);
    fs::remove(file);
}
