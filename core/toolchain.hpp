#ifndef PGR_TOOLCHAIN_HPP
#define PGR_TOOLCHAIN_HPP
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace pgr_toolchain {
enum class family { gcc, clang, msvc };
inline family classify(const std::string& compiler) {
    std::string name = std::filesystem::u8path(compiler).filename().u8string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){return std::tolower(c);});
    if (name == "cl" || name == "cl.exe") return family::msvc;
    if (name.find("clang") != std::string::npos) return family::clang;
    return family::gcc;
}
inline std::string name(family f) {
    return f == family::msvc ? "MSVC" : f == family::clang ? "Clang" : "GCC";
}
inline std::string selected_compiler(const std::string& compiler, const std::string& prefix) {
    if (prefix.empty()) return compiler;
    const auto filename = std::filesystem::u8path(compiler);
    // Explicit absolute/relative compiler paths always take precedence.
    if (filename.has_parent_path()) return compiler;
    if (compiler == "g++" || compiler == "gcc" || compiler == "clang" || compiler == "clang++")
        return prefix + compiler;
    return compiler;
}
inline std::string prefixed(const std::string& prefix, const char* tool) {
    return prefix + tool;
}
inline std::string c_compiler(const std::string& compiler) {
    namespace fs = std::filesystem;
    const fs::path path = fs::u8path(compiler);
    const std::string name = path.filename().u8string();
    if (classify(compiler) == family::msvc) return compiler;
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
    if (classify(compiler) == family::msvc) {
        args.push_back("-DCMAKE_C_COMPILER=" + compiler);
        args.push_back("-DCMAKE_CXX_COMPILER=" + compiler);
    } else if (c == compiler) {
        args.push_back("-DCMAKE_C_COMPILER=" + c);
    } else {
        args.push_back("-DCMAKE_CXX_COMPILER=" + compiler);
        // An extracted Pack may contain only g++ without a matching gcc.
        // Let CMake discover C compiler normally rather than forcing a broken path.
        if (!c.empty() && (!std::filesystem::u8path(c).has_parent_path() || std::filesystem::is_regular_file(std::filesystem::u8path(c))))
            args.push_back("-DCMAKE_C_COMPILER=" + c);
    }
}
}
#endif
