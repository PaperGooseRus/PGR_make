#include "common.hpp"
#include "core/safe_paths.hpp"
using namespace pgr_test;
int main() { try {
    sandbox t("paths");
    auto project=t.path/"sources";
    auto store=t.path/"metadata";
    fs::create_directories(project);fs::create_directories(store);
    put(project/"build_user"/"notes.txt","valuable data");
    std::string error;
    check(!pgr_paths::clean("build_user",project,store,error),"deleted unowned files");
    check(fs::exists(project/"build_user"/"notes.txt"),"original data removed");
    check(!pgr_paths::prepare("build_user",project,store,error),"adopted non-empty directory");
    for (auto candidate : {".","..","../oops","build/..","build/../../else","/"})
        check(!pgr_paths::safe_build_dir(candidate,project),std::string("unsafe path accepted: ")+candidate);
    check(pgr_paths::prepare(".pgr_build/sample",project,store,error),"owned build preparation: "+error);
    check(fs::is_regular_file(project/".pgr_build/sample/.pgr_make_build_owner"),"marker missing");
    put(project/".pgr_build/sample/output.exe","generated data");
    check(!pgr_paths::clean(".pgr_build/sample",project,t.path/"foreign",error),"foreign ownership accepted");
    check(fs::exists(project/".pgr_build/sample/output.exe"),"foreign cleanup deleted files");
    check(pgr_paths::clean(".pgr_build/sample",project,store,error),"owned cleanup: "+error);
    check(!fs::exists(project/".pgr_build/sample"),"owned build remained");
    fs::create_directories(t.path/"external");
    std::error_code ec;
    fs::create_directory_symlink(t.path/"external",project/"build-link",ec);
    if (!ec) check(!pgr_paths::safe_build_dir(project/"build-link",project),"symlink path accepted");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
