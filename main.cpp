/**
 * @file main.cpp
 * @brief TUI по умолчанию, скриптовый конвейер — по флагу --demo.
 */

#include <filesystem>
#include <iostream>
#include <string>

#include "core/read.hpp"
#include "core/write.hpp"
#include "core/make.hpp"
#include "core/TUI.hpp"

/**
 * @brief Демонстрационный прогон с защитой от отсутствующих файлов.
 * @return 0 при успехе, 1 при ошибке.
 */
static int run_demo() {
    std::filesystem::create_directories("build");
    std::filesystem::create_directories("build/test");

    if (!std::filesystem::exists("test/main.cpp")) {
        std::cerr << "test/main.cpp not found\n";
        return 1;
    }

    std::cout << "=== Step 1: read from terminal ===\n";

    read_t reader;
    reader.read();

    if (reader.data().empty()) {
        std::cerr << "No input from terminal\n";
        return 1;
    }

    write_t writer_step1("build/build_step1.txt");
    if (writer_step1.status() != 0x00) {
        std::cerr << "Failed to open build/build_step1.txt\n";
        return 1;
    }
    if (writer_step1.write(reader.data()) != 0x00) {
        std::cerr << "Failed to write build/build_step1.txt\n";
        return 1;
    }

    std::cout << "=== Step 2: read back from file ===\n";

    reader.clear();
    int read_st = reader.read("build/build_step1.txt");
    if (read_st != 0x00 && read_st != 0x01 && read_st != 0x02) {
        std::cerr << "read failed, status = " << read_st << '\n';
        return 1;
    }

    for (const auto& s : reader.data()) {
        std::cout << "  " << s << '\n';
    }

    std::cout << "=== Step 3: configure make_t ===\n";

    make_t builder;
    builder.set_language("c++");
    builder.set_compiler(make_t::compiler::GPP);
    builder.set_source_file("test/main.cpp");
    builder.set_output_file("build/test/main.exe");
    builder.set_optimization("-Os");

    if (builder.build_args() != 0x00) {
        std::cerr << "Invalid configuration\n";
        return 1;
    }

    for (const auto& s : builder.args()) {
        std::cout << "  " << s << '\n';
    }

    write_t writer_step2("build/build_step2.txt");
    if (writer_step2.status() != 0x00) {
        std::cerr << "Failed to open build/build_step2.txt\n";
        return 1;
    }
    if (builder.write_args(writer_step2) != 0x00) {
        std::cerr << "Failed to write build/build_step2.txt\n";
        return 1;
    }

    std::cout << "=== Step 4: build command string ===\n";

    builder.build_command(builder.args());
    if (builder.command().empty()) {
        std::cerr << "Command is empty\n";
        return 1;
    }
    std::cout << "  cmd: " << builder.command() << '\n';

    write_t writer_step3("build/build_step3.txt");
    if (writer_step3.status() != 0x00) {
        std::cerr << "Failed to open build/build_step3.txt\n";
        return 1;
    }
    if (builder.write_command(writer_step3) != 0x00) {
        std::cerr << "Failed to write build/build_step3.txt\n";
        return 1;
    }

    std::cout << "=== Step 5: execute command ===\n";

    int run_st = builder.run_file("build/build_step3.txt");
    if (run_st != 0x00) {
        std::cerr << "run_file failed, status = " << run_st << '\n';
        return 1;
    }

    std::cout << "=== Done ===\n";
    return 0;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--demo") {
            return run_demo();
        }

        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--demo]\n"
                      << "  (no args)  интерактивный TUI\n"
                      << "  --demo     скриптовый конвейер\n";
            return 0;
        }

        std::cerr << "Unknown argument: " << arg << '\n';
        return 1;
    }

    std::filesystem::create_directories("build");
    std::filesystem::create_directories("build/test");

    tui_t tui;
    tui.run();
    return 0;
}