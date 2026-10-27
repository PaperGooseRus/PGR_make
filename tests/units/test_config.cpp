#include "common.hpp"
#include "core/make.hpp"
using namespace pgr_test;
int main() { try {
    sandbox t("config");
    make_t x;
    x.set_compiler("g++");x.set_standard("-std=c++17");
    x.set_source_files({"src/a.cpp","src/name with spaces.cpp"});
    x.set_output_file("build/app.exe");x.set_warnings({"-Wall","-Wextra"});
    x.set_packages({"ZLIB|ZLIB::ZLIB"});
    check(x.validate_configuration(true)==0,"valid config rejected");
    check(x.build_args()==0,"valid arguments rejected");
    check(x.save((t.path/"config.cfg").string())==0,"config save failed");
    make_t restored;
    check(restored.load((t.path/"config.cfg").string())==0,"config load failed");
    check(restored.config()==x.config(),"config roundtrip lost fields");
    restored.set_sanitizers({"-fsanitize=address","-fsanitize=thread"});
    check(restored.validate_configuration(true)!=0,"incompatible sanitizers accepted");
    restored.set_sanitizers({});restored.set_warnings({"-Wall\nBAD"});
    check(restored.validate_configuration(true)!=0,"newline flag accepted");
    restored.set_warnings({});restored.set_packages({"Invalid Package Name"});
    check(restored.validate_configuration(true)!=0,"invalid CMake package name accepted");
    restored.set_packages({});restored.set_source_files({});
    check(restored.validate_configuration(true)!=0,"empty source list accepted");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
