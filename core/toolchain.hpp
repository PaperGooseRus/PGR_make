#ifndef PGR_TOOLCHAIN_HPP
#define PGR_TOOLCHAIN_HPP
#include <filesystem>
#include <string>
#include <vector>

namespace pgr_toolchain {
inline std::string c_compiler(const std::string& compiler) {
    namespace fs = std::filesystem;
    const fs::path path = fs::u8path(compiler);
    const std::string name = path.filename().u8string();
    std::string translated;
    if (name == "g++") translated = "gcc";
    else if (name == "g++.exe") translated = "gcc.exe";
    else if (name == "clang++") translated = "clang";
    else if (name == "clang++.exe") translated = "clang.exe";
    else {
        auto pos = name.rfind("g++");
        if (pos != std::string::npos && pos + 3 == name.size())
            translated = name.substr(0, pos) + "gcc";
        else if (pos != std::string::npos && name.substr(pos) == "g++.exe")
            translated = name.substr(0, pos) + "gcc.exe";
        else if (name == "gcc" || name == "gcc.exe" || name == "clang" || name == "clang.exe")
            return compiler;
        else return {};
    }
    return path.has_parent_path() ? (path.parent_path() / fs::u8path(translated)).u8string() : translated;
}

inline void cmake_compilers(const std::string& compiler, std::vector<std::string>& args) {
    if (compiler.empty()) return;
    const auto c = c_compiler(compiler);
    if (c == compiler) {
        args.push_back("-DCMAKE_C_COMPILER=" + c);
    } else {
        args.push_back("-DCMAKE_CXX_COMPILER=" + compiler);
        if (!c.empty()) args.push_back("-DCMAKE_C_COMPILER=" + c);
    }
}
}
#endif
