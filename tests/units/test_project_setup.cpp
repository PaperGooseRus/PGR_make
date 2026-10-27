#include "common.hpp"
#include "core/project_setup.hpp"
using namespace pgr_test;
int main() {try {
    sandbox t("projectsetup");
    pgr_workspace::registry_t registry(t.path/"PGR");
    std::string error;
    check(registry.ensure(error),"unable to create registry");
    auto source=t.path/"external";
    put(source/"src/main.c","int main(void){return 0;}\n");
    pgr_workspace::project_t p;
    check(pgr_project::create_project(registry,"Hello",source,false,p,error),"registration failed");
    make_t config;
    check(config.load(p.config().string())==0,"cannot load generated configuration");
    check(config.config().compiler=="gcc","C-only project did not default to gcc");
    check(config.config().source_files.size()==1,"source discovery failed");
    check(!pgr_project::create_project(registry,"Hello",source,false,p,error),"overwrote existing project");
    auto cpp=t.path/"cpp";
    put(cpp/"main.cpp","int main(){return 0;}\n");
    check(pgr_project::create_project(registry,"Cpp",cpp,false,p,error),"C++ registration failed");
    check(config.load(p.config().string())==0 && config.config().compiler=="g++","C++ default compiler");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
