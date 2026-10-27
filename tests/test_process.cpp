#include "../core/process_runner.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
namespace fs=std::filesystem;
int main(int argc,char** argv) {
    if (argc>=3 && std::string(argv[1])=="--grandchild") {
        std::this_thread::sleep_for(std::chrono::milliseconds(1800));
        std::ofstream(argv[2])<<"unexpected";
        return 0;
    }
    if (argc>=3 && std::string(argv[1])=="--child") {
        // Run child of child, to check cancellation covers descendants.
        return pgr_process::run({argv[0],"--grandchild",argv[2]},true);
    }
    const auto stamp=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto sentinel=fs::temp_directory_path()/("pgr_cancel_"+stamp+".txt");
    pgr_process::cancel_token token;
    int code=-1;
    auto start=std::chrono::steady_clock::now();
    std::thread runner([&](){
        pgr_process::options opt;opt.quiet=true;opt.cancel=&token;
        code=pgr_process::run_ex({fs::absolute(argv[0]).string(),"--child",sentinel.string()},opt);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    token.cancel(); runner.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(1900));
    const bool artifact_exists=fs::exists(sentinel);
    bool safe=(code==130 && !artifact_exists &&
        std::chrono::steady_clock::now()-start<std::chrono::seconds(4));
    std::error_code ec;fs::remove(sentinel,ec);
    if (!safe) {std::cerr<<"cancel failed rc="<<code<<" exists="<<artifact_exists
        <<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";return 1;}
    std::cout<<"PASS: cancellation terminates descendants\n";
}
