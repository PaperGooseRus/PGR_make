#include <iostream>

#include "read.hpp"
#include "write.hpp"

int main(int argc, char* argv[]) {
    std::cout << "Hello World!" << std::endl;

    read_t read;
    read.read();

    write_t write("build/build_step1.txt");
    write.write(read.data());

    return 0x00;
}