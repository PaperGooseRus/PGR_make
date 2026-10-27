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
    if (!(history.size()==2 && history[0].result=="2" && history[1].mode=="generated")) {
        std::cerr << "History mismatch: count=" << history.size();
        for (const auto& entry:history)
            std::cerr << " [result=" << entry.result << ",mode=" << entry.mode << "]";
        std::cerr << '\n';
        return 1;
    }
    // A history file may originate from Windows text-mode I/O (CRLF).
    {
        std::ofstream out(base/"store"/"history.log",std::ios::binary|std::ios::trunc);
        out << "1700000002000\t1\t111\tdirect\r\n";
        out << "broken\r\n";
        out << "1700000003000\t0\t200\tnative\r\n";
    }
    auto windows_history=load_history(base/"store"/"history.log");
    if (!(windows_history.size()==2 && windows_history[0].mode=="native" &&
          windows_history[1].mode=="direct")) {
        std::cerr << "CRLF history parsing mismatch\n";
        return 1;
    }
    fs::remove_all(base);
    std::cout<<"PASS: GUI mode persistence, path checks, history and defaults\n";
}
