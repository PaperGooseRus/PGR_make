#include "common.hpp"
#include "core/toolchain_manager.hpp"
using namespace pgr_test;
int main() { try {
    sandbox t("tools");
    const auto pack=t.path/"Pack";
    auto a=pack/"GCC"/"14"/"bin"/"g++";
#ifdef _WIN32
    a+= ".exe";
#endif
    put(a,"mock");
    auto chosen=pgr_tools::find(pack,"g++",{},false);
    check(!chosen.empty() && fs::equivalent(chosen,a),"did not find versioned tool in Pack");
    check(pgr_tools::find(pack,"missing-tool",{},false).empty(),"silent fallback on explicit no-fallback");
    check(pgr_tools::find(pack,"missing-tool",{},true)==fs::path("missing-tool"),"explicit fallback failed");
    auto outside=t.path/"external tool";
    put(outside,"fake");
    check(pgr_tools::find(pack,outside.string(),{},false)==outside,"absolute tool path rejected");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
