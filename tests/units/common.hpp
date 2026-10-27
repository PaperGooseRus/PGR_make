#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <iostream>
namespace pgr_test {
namespace fs = std::filesystem;
inline void check(bool value, const std::string& reason) {
    if (!value) throw std::runtime_error(reason);
}
struct sandbox {
    fs::path path;
    explicit sandbox(const std::string& label) {
        auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path()/("pgr_unit_"+label+"_"+std::to_string(serial));
        fs::create_directories(path);
    }
    ~sandbox() { std::error_code ec; fs::remove_all(path,ec); }
    sandbox(const sandbox&)=delete;
    sandbox& operator=(const sandbox&)=delete;
};
inline void put(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);
    out << data;
    check(static_cast<bool>(out),"unable to write fixture");
}
}
