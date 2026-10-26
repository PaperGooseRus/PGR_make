/**
 * @file main.cpp
 * @brief Точка входа: интерактивный TUI для read_t/write_t/make_t.
 */

#include <filesystem>

#include "TUI.hpp"

int main() {
    // ofstream не создаёт каталоги — готовим их до первой записи.
    std::filesystem::create_directories("build");
    std::filesystem::create_directories("build/test");

    tui_t tui;
    tui.run();

    return 0;
}