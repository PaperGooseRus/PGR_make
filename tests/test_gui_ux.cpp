#include "../gui/ux_model.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
int main(int argc,char** argv) {
    assert(argc>1);
    const fs::path base=fs::u8path(argv[1]);
    fs::remove_all(base);
    fs::create_directories(base/"source"/"include");
    fs::create_directories(base/"store");
    std::ofstream(base/"source"/"CMakeLists.txt") << "cmake_minimum_required(VERSION 3.16)\n";
    std::ofstream(base/"source"/"main.cpp") << "int main(){}\n";
    using namespace pgr_gui_ux;
    assert(load_mode(base/"store",base/"source")==build_mode::native);
    assert(save_mode(base/"store",build_mode::direct));
    assert(load_mode(base/"store",base/"source")==build_mode::direct);
    assert(preflight(base/"source",build_mode::native,"",{}, {}, {}).empty());
    assert(preflight(base/"source",build_mode::direct,"g++",{"main.cpp"},{"include"},{}).empty());
    assert(!preflight(base/"source",build_mode::direct,"",{"missing.cpp"},{"not_found"},"missing.cmake").empty());
    assert(preflight(base/"source",build_mode::generated,"g++",{}, {}, {}).empty());
    assert(!preflight(base/"store",build_mode::native,"g++",{}, {}, {}).empty());
    std::ofstream(base/"store"/"history.log") << "1700000000000\t0\t123\tgenerated\ninvalid\n1700000001000\t2\t450\tnative\n";
    auto history=load_history(base/"store"/"history.log");
    assert(history.size()==2 && history[0].result=="2" && history[1].mode=="generated");
    fs::remove_all(base);
    std::cout<<"PASS: GUI mode persistence, path checks, history and defaults\n";
}
