#include "../core/workspace.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <chrono>
namespace fs=std::filesystem;
int main() {
    const auto stamp=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path home=fs::temp_directory_path() / ("pgr_registry_test_"+stamp);
    struct cleanup {fs::path p;~cleanup(){std::error_code ec;fs::remove_all(p,ec);} } final{home};
    const auto source=home/"sources",new_source=home/"relocated";
    std::error_code ec;
    fs::create_directories(source,ec);fs::create_directories(new_source,ec);
    std::ofstream(source/"important.txt") << "DO NOT DELETE";
    pgr_workspace::registry_t registry(home/"workspace");
    std::string error;
    if (!registry.ensure(error)) return 1;
    pgr_workspace::project_t p;
    if (!registry.create("Example",source,p,error)) {std::cerr<<error;return 2;}
    std::ofstream(p.config()) << "compiler=g++\nsource_files=main.cpp\n";
    if (!registry.clone_project("Example","Copy",error)) {std::cerr<<error;return 10;}
    if (!registry.rename_project("Example","New Example",error)) {std::cerr<<error;return 3;}
    if (!registry.relocate("New Example",new_source,error)) {std::cerr<<error;return 4;}
    if (!registry.read("New Example",p,error) || p.source_root!=fs::weakly_canonical(new_source)) return 5;
    fs::remove_all(new_source,ec);
    const auto offline=registry.entries();
    if (offline.size()!=2 ||
        !std::any_of(offline.begin(),offline.end(),[](const auto& e){return e.name=="New Example" && !e.available;})) return 6;
    if (!registry.archive("New Example",error)) {std::cerr<<error;return 7;}
    if (registry.entries().size()!=1 || registry.entries()[0].name!="Copy" ||
        !fs::is_regular_file(source/"important.txt")) return 8;
    if (!fs::is_directory(registry.projects_root()/".pgr_archived"/"New Example")) return 9;
    std::cout<<"PASS: rename, relocate, offline listing, archive, preserve sources\n";
}
