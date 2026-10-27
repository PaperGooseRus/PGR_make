#include "common.hpp"
#include "core/pack_bulk.hpp"
using namespace pgr_test;
using namespace pgr_pack_manager;
using namespace pgr_pack_bulk;

int main() {try {
    sandbox t("bulk_pack");
    check(compare_version("1.13.2","1.13.1")>0,"numeric patch ordering");
    check(compare_version("4.10.0","4.9.99")>0,"numeric minor ordering");
    check(compare_version("16.2","16.2.0")==0,"missing trailing zero");
    const auto platform_name="windows-x86_64";
    auto create=[&](std::string tool,std::string version)->package {
        package p;
        p.tool=tool;p.version=version;p.platform=platform_name;
        p.id=tool+"@"+version+"@"+p.platform;
        p.sha256=std::string(64,'a');p.url="https://example.test/"+p.id+".zip";
        p.executable="bin/"+tool+".exe";
        return p;
    };
    auto gcc=create("gcc","16.2.0");
    gcc.provides={{"clang","23.1.3","bin/clang++.exe"}};
    auto cmake=create("cmake","4.4.4");
    auto ninja_old=create("ninja","1.13.1");
    auto ninja_new=create("ninja","1.13.2");
    manager registry(t.path/"Pack",t.path);
    std::string error;
    check(registry.load(error),"empty registry loads");
    const auto p=latest({cmake,ninja_old,gcc,ninja_new},registry,platform_name);
    check(p.ready(),"all four required tool categories should be covered");
    check(p.downloads.size()==3,"GCC+Clang bundle must be downloaded only once");
    check(p.downloads[0].id==gcc.id,"compiler bundle first");
    check(p.downloads[1].id==cmake.id,"CMake second");
    check(p.downloads[2].id==ninja_new.id,"newest Ninja version selected");
    const auto missing=latest({cmake,ninja_new},registry,platform_name);
    check(!missing.ready() && missing.unavailable.size()==2,"report both missing compiler families");
    // A local Pack catalog extends the new builtin catalog without hiding its updates.
    put(t.path/"tools"/"catalog.json", R"({"packages":[
       {"id":"ninja@1.13.2@windows-x86_64","tool":"ninja","version":"1.13.2","platform":"windows-x86_64","url":"https://example.test/ninja.zip","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","executable":"ninja.exe"}]})");
    put(t.path/"Pack"/"catalog.json", R"({"packages":[
       {"id":"ninja@1.13.1@windows-x86_64","tool":"ninja","version":"1.13.1","platform":"windows-x86_64","url":"https://example.test/old.zip","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","executable":"ninja.exe"}]})");
    auto merged=merged_catalog(t.path,t.path/"Pack",error);
    check(error.empty() && merged.size()==2,"old local catalog must not conceal new bundled versions");
    check(latest({gcc,cmake,merged[0],merged[1]},registry,platform_name).downloads.back().version=="1.13.2", "merge must prefer latest release");
    // Verify that metadata for bundled tools is validated, not blindly trusted.
    pgr_json::value obj;
    check(pgr_json::parser(R"({"id":"gcc@16.2.0@windows-x86_64","tool":"gcc","version":"16.2.0","platform":"windows-x86_64","url":"https://example.test/gcc.zip","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","executable":"mingw64/bin/g++.exe","provides":[{"tool":"clang","version":"23.1.3","executable":"mingw64/bin/clang++.exe"}]})").parse(obj),"fixture parsed");
    package parsed;
    check(parse_package(obj,parsed) && parsed.provides.size()==1,"bundled companion must parse");
    check(id_for(parsed,"clang")=="clang@23.1.3@windows-x86_64","companion id");
    check(!version_for(parsed,"ninja").size(),"unprovided tool should not be invented");
    // Shared managed installation: retiring one record must retire the entire
    // bundle, and any project pin for a companion must block that operation.
    const auto root=t.path/"Pack"/"gcc"/"16.2.0-linux";
    put(root/"toolchain"/"bin"/"g++","fake binary");
    installed main;
    main.id="gcc@16.2.0@linux";main.tool="gcc";main.version="16.2.0";
    main.platform="linux";main.executable="toolchain/bin/g++";
    main.root=root;
    installed companion=main;
    companion.id="clang@23.1.3@linux";companion.tool="clang";
    companion.version="23.1.3";companion.executable="toolchain/bin/clang++";
    put(t.path/"Pack"/"registry.json","{\"schema\":1,\"installed\":["+serialize(main)+","+serialize(companion)+"]}");
    manager loaded(t.path/"Pack",t.path);
    check(loaded.load(error),"bundle registry fixture loads: "+error);
    check(!loaded.remove(main.id,{companion.id},error),"pinned bundle companion must block retirement");
    check(fs::is_directory(root),"rejected retirement moved live data");
    error.clear();
    check(loaded.remove(main.id,{},error),"atomic retirement failed: "+error);
    check(loaded.entries().empty(),"retired bundle left dangling companion metadata");
    check(fs::is_directory(t.path/"Pack"/"Retired"/main.id),"retired bundle missing quarantine");
    return 0;
} catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
