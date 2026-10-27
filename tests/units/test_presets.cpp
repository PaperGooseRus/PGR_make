#include "common.hpp"
#include "core/presets.hpp"
#include "core/json_reader.hpp"
using namespace pgr_test;
int main() {try {
    sandbox t("presets");
    pgr_workspace::project_t p;
    p.name="Test";p.source_root=t.path/"source files";p.store_root=t.path/"PGR"/"Project"/"Test";
    fs::create_directories(p.source_root/"includes");fs::create_directories(p.store_root);
    auto stage=p.stage();fs::create_directories(stage);
    make_t::config_t cfg;
    cfg.package_dirs={"includes"};
    std::vector<std::string> args;std::string error;
    check(pgr_presets::configure_toolchain(p,cfg,args,error),"valid CMake package prefix");
    check(args.size()==1 && args[0].find("CMAKE_PREFIX_PATH=")!=std::string::npos,"prefix config not passed to CMake");
    check(!pgr_presets::write(p,cfg,error),"wrote presets outside generated owner tree");
    put(stage/".pgr_make_generated_owner","test owner");
    check(pgr_presets::write(p,cfg,error),"cannot write owner-controlled presets");
    pgr_json::value root;
    {std::ifstream f(stage/"CMakePresets.json");std::string text((std::istreambuf_iterator<char>(f)),{});
     check(pgr_json::parser(text).parse(root),"generated CMakePresets.json invalid JSON");}
    check(root.find("version") && root.find("version")->number==3,"wrong CMake preset schema");
    cfg.package_manager="conan";
    check(!pgr_presets::configure_toolchain(p,cfg,args,error),"Conan without toolchain accepted");
    cfg.package_manager="none";cfg.toolchain_file="absent.cmake";
    check(!pgr_presets::configure_toolchain(p,cfg,args,error),"missing toolchain accepted");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
