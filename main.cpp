/**
 * @file main.cpp
 * @brief Демонстрационный драйвер: ввод с терминала → файл → чтение
 *        обратно → сборка → запуск.
 *
 * Конвейер:
 *  1. Прочитать строки из std::cin (до EOF или пустой строки).
 *  2. Записать их в @c build/build_step1.txt.
 *  3. Прочитать файл обратно и напечатать содержимое.
 *  4. Сконфигурировать @ref make_t и вывести @c build/build_step2.txt
 *     (список аргументов).
 *  5. Склеить аргументы в строку команды и вывести @c build/build_step3.txt.
 *  6. Прочитать команду из @c build/build_step3.txt и выполнить её.
 */

#include <filesystem>
#include <iostream>
#include <string>

#include "read.hpp"
#include "write.hpp"
#include "make.hpp"

int main() {
    // --- Подготовка каталогов ---
    // ofstream не создаёт каталоги — создаём явно до первой записи;
    // каталог для выходного exe тоже нужен заранее, иначе g++ упадёт.
    std::filesystem::create_directories("build");
    std::filesystem::create_directories("build/test");

    std::cout << "=== Step 1: read from terminal ===\n";

    read_t reader;
    reader.read_terminal();

    if (reader.data().empty()) {
        std::cerr << "No input from terminal\n";
        return 1;
    }

    write_t writer_step1("build/build_step1.txt");
    if (writer_step1.write(reader.data()) != 0x00) {
        std::cerr << "Failed to write build/build_step1.txt\n";
        return 1;
    }

    std::cout << "=== Step 2: read back from file ===\n";

    reader.clear(); // чтобы не смешивать с терминальным вводом
    int read_st = reader.read_file("build/build_step1.txt");

    // 0x01 (EOF) и 0x02 (пустая строка) — нормальное завершение.
    if (read_st != 0x00 && read_st != 0x01 && read_st != 0x02) {
        std::cerr << "read_file failed, status = " << read_st << '\n';
        return 1;
    }

    for (const auto& s : reader.data()) {
        std::cout << "  " << s << '\n';
    }

    std::cout << "=== Step 3: configure make_t ===\n";

    make_t builder;
    builder.set_language("c++");
    builder.set_compiler(make_t::compiler::GPP);
    builder.set_target_cpp("test/main.cpp");
    builder.set_target_app("build/test/main.exe");
    builder.set_optimization("-Os");
    builder.make_arr();

    for (const auto& s : builder.data()) {
        std::cout << "  " << s << '\n';
    }

    write_t writer_step2("build/build_step2.txt");
    if (writer_step2.write(builder.data()) != 0x00) {
        std::cerr << "Failed to write build/build_step2.txt\n";
        return 1;
    }

    std::cout << "=== Step 4: build command string ===\n";

    builder.make(builder.data());
    const std::string& cmd = builder.get_cmd();

    if (cmd.empty()) {
        std::cerr << "Command is empty\n";
        return 1;
    }

    std::cout << "  cmd: " << cmd << '\n';

    write_t writer_step3("build/build_step3.txt");
    if (writer_step3.write(cmd) != 0x00) {
        std::cerr << "Failed to write build/build_step3.txt\n";
        return 1;
    }

    std::cout << "=== Step 5: execute command ===\n";

    // Вариант A: выполнить из памяти (быстрее, без чтения файла).
    // int run_st = builder.make_run();
    //
    // Вариант B: прочитать из файла и выполнить.
    int run_st = builder.make_run("build/build_step3.txt");

    if (run_st != 0x00) {
        std::cerr << "make_run failed, status = " << run_st << '\n';
        return 1;
    }

    std::cout << "=== Done ===\n";
    return 0;
}