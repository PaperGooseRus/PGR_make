#include "common.hpp"
#include "core/toolchain.hpp"
using namespace pgr_test;
int main() {try {
    using namespace pgr_toolchain;
    check(classify("C:/VS/Tools/cl.exe")==family::msvc,"MSVC family");
    check(classify("clang++")==family::clang,"Clang family");
    check(classify("/opt/cross/arm-none-eabi-g++")==family::gcc,"GCC family");
    check(c_compiler("g++") == "gcc", "G++ C compiler");
    check(c_compiler("clang++") == "clang", "Clang C compiler");
    check(c_compiler("arm-none-eabi-g++") == "arm-none-eabi-gcc", "cross C compiler");
    check(c_compiler("cl.exe") == "cl.exe", "MSVC same C compiler");
    check(selected_compiler("g++","arm-none-eabi-")=="arm-none-eabi-g++", "prefix resolution");
    check(selected_compiler("/usr/bin/g++","arm-none-eabi-")=="/usr/bin/g++","explicit compiler cannot be overridden");
    std::vector<std::string> flags;
    cmake_compilers("g++",flags);
    check(flags.size()==2, "CMake C/C++ compiler pair");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
