#ifndef PGR_GUI_UX_MODEL_HPP
#define PGR_GUI_UX_MODEL_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace pgr_gui_ux {
namespace fs = std::filesystem;

enum class build_mode { generated=0, native=1, direct=2 };
inline const char* mode_id(build_mode mode) {
    switch (mode) { case build_mode::native: return "native"; case build_mode::direct: return "direct"; default: return "generated"; }
}
inline build_mode parse_mode(const std::string& value) {
    if (value=="native") return build_mode::native;
    if (value=="direct") return build_mode::direct;
    return build_mode::generated;
}
inline fs::path mode_file(const fs::path& project_store) { return project_store / ".pgr_gui_mode"; }
inline build_mode load_mode(const fs::path& project_store, const fs::path& source_root) {
    std::error_code link_error;
    if (fs::is_symlink(mode_file(project_store),link_error)) return build_mode::generated;
    std::ifstream in(mode_file(project_store), std::ios::binary);
    std::string value;
    if (in && std::getline(in,value)) return parse_mode(value);
    // Existing CMake projects should work on first launch without regenerated CMake.
    std::error_code ec;
    return fs::is_regular_file(source_root / "CMakeLists.txt",ec) ? build_mode::native : build_mode::generated;
}
inline bool save_mode(const fs::path& project_store, build_mode mode) {
    std::error_code link_error;
    if (fs::is_symlink(mode_file(project_store),link_error)) return false;
    std::ofstream out(mode_file(project_store), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << mode_id(mode) << '\n';
    return static_cast<bool>(out);
}
struct issue { std::string field, message; };
inline fs::path resolved(const fs::path& root, const std::string& path) {
    auto p=fs::u8path(path);
    return (p.is_absolute()?p:root/p).lexically_normal();
}
inline std::vector<issue> preflight(const fs::path& source_root, build_mode mode,
                                    const std::string& compiler,
                                    const std::vector<std::string>& sources,
                                    const std::vector<std::string>& includes,
                                    const std::string& toolchain_file) {
    std::vector<issue> issues;
    std::error_code ec;
    if (!fs::is_directory(source_root,ec)) {
        issues.push_back({"source_root","Каталог исходников недоступен"});
        return issues;
    }
    if (mode==build_mode::native && !fs::is_regular_file(source_root/"CMakeLists.txt",ec))
        issues.push_back({"build_mode","Для режима существующего CMake нужен CMakeLists.txt в корне проекта"});
    if (mode==build_mode::direct) {
        if (compiler.empty()) issues.push_back({"compiler","Не указан компилятор для прямой сборки"});
        if (sources.empty()) issues.push_back({"source_files","Список исходников пуст"});
    }
    // For generated mode automatic source scanning can be used; missing entries are still errors.
    for(const auto& source:sources) if(mode!=build_mode::native && !source.empty() && !fs::is_regular_file(resolved(source_root,source),ec))
        issues.push_back({"source_files","Не найден исходник: "+source});
    for(const auto& folder:includes) if(mode!=build_mode::native && !folder.empty() && !fs::is_directory(resolved(source_root,folder),ec))
        issues.push_back({"include_dirs","Не найден include-каталог: "+folder});
    if (!toolchain_file.empty() && !fs::is_regular_file(resolved(source_root,toolchain_file),ec))
        issues.push_back({"toolchain_file","Не найден CMake toolchain-файл: "+toolchain_file});
    return issues;
}

struct history_entry { std::string timestamp, result, elapsed, mode; };
inline std::vector<history_entry> load_history(const fs::path& file, std::size_t limit=100) {
    std::ifstream in(file,std::ios::binary);
    std::vector<history_entry> result;
    std::string line;
    while(std::getline(in,line)) {
        std::istringstream ss(line);
        history_entry value;
        if (!std::getline(ss,value.timestamp,'\t') || !std::getline(ss,value.result,'\t') ||
            !std::getline(ss,value.elapsed,'\t') || !std::getline(ss,value.mode,'\t')) continue;
        // Tolerate LF and CRLF history written by older releases or editors.
        for (auto* field : {&value.timestamp, &value.result, &value.elapsed, &value.mode}) {
            if (!field->empty() && field->back() == '\r') field->pop_back();
        }
        try { (void)std::stoll(value.timestamp); (void)std::stoll(value.elapsed); }
        catch (...) { continue; }
        result.push_back(std::move(value));
        if(result.size()>limit) result.erase(result.begin());
    }
    std::reverse(result.begin(),result.end());
    return result;
}
inline bool is_native_executable(const fs::path& path) {
#ifdef _WIN32
    return path.extension()==".exe";
#else
    std::error_code ec;
    return fs::is_regular_file(path,ec) && (fs::status(path,ec).permissions() & fs::perms::owner_exec)!=fs::perms::none;
#endif
}
} // namespace pgr_gui_ux
#endif
