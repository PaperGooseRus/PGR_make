#ifndef PGR_WORKSPACE_HPP
#define PGR_WORKSPACE_HPP

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <cstdlib>
#endif

namespace pgr_workspace {
namespace fs = std::filesystem;

inline std::string utf8(const fs::path& p) { return p.u8string(); }
inline fs::path from_utf8(const std::string& p) { return fs::u8path(p); }

inline fs::path executable_directory() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
    if (n == 0 || n >= buffer.size()) return fs::current_path();
    buffer.resize(n);
    return fs::path(buffer).parent_path();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
#endif
}

inline bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 128 || name == "." || name == "..") return false;
    if (name.front() == ' ' || name.front() == '.' || name.back() == '.' || name.back() == ' ') return false;
    // UTF-8 letters are accepted; forbid Windows separators/reserved characters.
    for (unsigned char c : name)
        if (c < 32 || c == 127 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') return false;
    std::string upper = name.substr(0, name.find('.'));
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") return false;
    if (upper.size() == 4 && ((upper.rfind("COM", 0) == 0) || (upper.rfind("LPT", 0) == 0)) && upper[3] >= '1' && upper[3] <= '9') return false;
    return true;
}

inline std::string cmake_target_name(const std::string& name) {
    std::string safe;
    for (unsigned char c : name) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_') safe += char(c);
        else if (c == ' ' || c == '.' || c == '-') safe += '_';
    }
    if (safe.empty() || (safe.front() >= '0' && safe.front() <= '9')) safe = "PGR_" + safe;
    return safe;
}

struct project_t {
    std::string name;
    fs::path source_root;
    fs::path store_root;
    fs::path config() const { return store_root / "config.cfg"; }
    fs::path profiles() const { return store_root / "profiles"; }
    fs::path log() const { return store_root / "work.log"; }
    fs::path stage() const { return store_root / "generated_cmake"; }
    fs::path build() const { return source_root / ".pgr_build" / from_utf8(name); }
};

// Discover compilable inputs without following symlinks or entering build/IDE folders.
inline std::vector<std::string> discover_sources(const fs::path& root) {
    std::vector<std::string> result;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && result.size() < 4096; it.increment(ec)) {
        if (it->is_symlink(ec)) { it.disable_recursion_pending(); continue; }
        if (it->is_directory(ec)) {
            auto name = it->path().filename().u8string();
            if (name == ".git" || name == ".pgr_build" || name == ".vs" ||
                name == "build" || name == "build_cmake" || name == "cmake-build-debug" ||
                name == "cmake-build-release" || name == "bin" || name == "obj" ||
                name == "out" || name.rfind("build_", 0) == 0 ||
                name.rfind("build-", 0) == 0) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = it->path().extension().u8string();
        if (ext != ".c" && ext != ".cpp" && ext != ".cc" && ext != ".cxx") continue;
        const auto relative = it->path().lexically_relative(root);
        if (!relative.empty()) result.push_back(relative.generic_u8string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

class registry_t {
    fs::path root_;
public:
    explicit registry_t(fs::path exe_root) : root_(std::move(exe_root)) {}
    const fs::path& root() const { return root_; }
    fs::path projects_root() const { return root_ / "Project"; }
    fs::path pack_root() const { return root_ / "Pack"; }

    bool ensure(std::string& error) const {
        std::error_code ec;
        fs::create_directories(projects_root(), ec);
        if (!ec) fs::create_directories(pack_root(), ec);
        if (ec) { error = ec.message(); return false; }
        return true;
    }

    bool read(const std::string& name, project_t& result, std::string& error) const {
        if (!valid_name(name)) { error = "Invalid project name"; return false; }
        fs::path dir = projects_root() / from_utf8(name);
        std::error_code ec;
        if (!fs::is_directory(dir, ec) || fs::is_symlink(dir, ec)) {
            error = "Project not found or project directory is a symlink: " + name;
            return false;
        }
        std::ifstream input(dir / "project.pgr", std::ios::binary);
        if (!input) { error = "Missing project.pgr for " + name; return false; }
        std::string line;
        std::string source;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("source=", 0) == 0) source = line.substr(7);
        }
        if (source.empty() || source.find_first_of("\r\n") != std::string::npos) {
            error = "Invalid source directory in " + name;
            return false;
        }
        fs::path origin = from_utf8(source);
        if (!origin.is_absolute() || !fs::is_directory(origin, ec)) {
            error = "Project source directory not found: " + source;
            return false;
        }
        result = {name, fs::weakly_canonical(origin), fs::weakly_canonical(dir)};
        return true;
    }

    struct entry_t {
        std::string name;
        fs::path source;
        bool available = false;
        std::string problem;
    };
    std::vector<entry_t> entries() const {
        std::vector<entry_t> found;
        std::error_code ec;
        for (fs::directory_iterator it(projects_root(), fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_directory() || it->is_symlink()) continue;
            const std::string name = utf8(it->path().filename());
            if (!valid_name(name) || name.front()=='.') continue;
            project_t p;
            std::string problem;
            bool available = read(name,p,problem);
            fs::path source = available ? p.source_root : fs::path{};
            if (!available) {
                std::ifstream meta(it->path()/"project.pgr", std::ios::binary);
                std::string line;
                while (std::getline(meta,line))
                    if (line.rfind("source=",0)==0) { source=from_utf8(line.substr(7)); break; }
            }
            found.push_back({name,source,available,problem});
        }
        std::sort(found.begin(),found.end(),[](const entry_t&a,const entry_t&b){return a.name<b.name;});
        return found;
    }

    // These operations mutate only registry metadata. Source files are never deleted.
    bool relocate(const std::string& name, const fs::path& new_source, std::string& error) const {
        project_t project;
        if (!valid_name(name)) { error="Invalid project name"; return false; }
        const fs::path dir=projects_root()/from_utf8(name);
        std::error_code ec;
        if (!fs::is_directory(dir,ec) || fs::is_symlink(dir,ec) || new_source.empty()) {
            error="Invalid project or source directory"; return false;
        }
        fs::path src=fs::weakly_canonical(new_source,ec);
        if (ec || !fs::is_directory(src,ec) || src==src.root_path()) {
            error="Source folder must already exist"; return false;
        }
        fs::path registry=fs::weakly_canonical(projects_root(),ec);
        if (ec) { error=ec.message(); return false; }
        auto it=src.begin(); bool inside=true;
        for (const auto& component : registry) {
            if (it==src.end() || *it!=component) {inside=false;break;}
            ++it;
        }
        if (inside) {error="Source cannot be inside registry";return false;}
        fs::path temp=dir/"project.pgr.new";
        if (fs::exists(temp,ec) || fs::is_symlink(dir/"project.pgr",ec)) {
            error="Unexpected metadata or pending edit; refusing overwrite";return false;
        }
        {
            std::ofstream stream(temp,std::ios::binary|std::ios::trunc);
            stream << "# PGR_make project record v1\nsource=" << utf8(src) << '\n';
            if (!stream) { fs::remove(temp,ec); error="Cannot write metadata"; return false; }
        }
        // Windows rename cannot atomically replace an existing file; keep a backup.
        fs::path original=dir/"project.pgr", backup=dir/"project.pgr.bak";
        if (fs::exists(backup,ec) || !fs::is_regular_file(original,ec)) {
            fs::remove(temp,ec); error="Missing metadata or previous backup"; return false;
        }
        fs::rename(original,backup,ec);
        if (ec) { fs::remove(temp,ec); error="Cannot back up metadata"; return false; }
        fs::rename(temp,original,ec);
        if (ec) { std::error_code revert; fs::rename(backup,original,revert); error="Cannot install metadata";return false; }
        fs::remove(backup,ec); return true;
    }
    bool rename_project(const std::string& old_name, const std::string& new_name, std::string& error) const {
        if (!valid_name(old_name) || !valid_name(new_name)) {error="Invalid project name";return false;}
        const auto origin=projects_root()/from_utf8(old_name), destination=projects_root()/from_utf8(new_name);
        std::error_code ec;
        if (!fs::is_directory(origin,ec) || fs::is_symlink(origin,ec) || fs::exists(destination,ec)) {
            error="Missing project or target already exists";return false;
        }
        fs::rename(origin,destination,ec);
        if (ec) {error=ec.message();return false;}
        // Owned build artifacts from old name remain in source folder and are not removed.
        return true;
    }
    bool clone_project(const std::string& source_name, const std::string& clone_name,
                       std::string& error) const {
        project_t original, created;
        error.clear();
        if (!read(source_name,original,error)) return false;
        if (!create(clone_name,original.source_root,created,error)) return false;
        std::error_code ec;
        const auto copy_safe=[&](const fs::path& src,const fs::path& dst)->bool {
            if (!fs::is_regular_file(src,ec) || fs::is_symlink(src,ec)) return false;
            return fs::copy_file(src,dst,fs::copy_options::none,ec) && !ec;
        };
        if (!copy_safe(original.config(),created.config())) {
            fs::remove_all(created.store_root,ec);
            error="Cannot duplicate config.cfg";return false;
        }
        if (fs::is_directory(original.profiles(),ec)) {
            for (fs::directory_iterator it(original.profiles(),ec),end;!ec && it!=end;it.increment(ec)) {
                if (it->path().extension()!=".cfg" || !copy_safe(it->path(),created.profiles()/it->path().filename())) {
                    error="Cannot copy one of the profiles (symlinks are forbidden)";break;
                }
            }
        }
        if (!error.empty()) {
            fs::remove_all(created.store_root,ec);
            return false;
        }
        return true;
    }
    bool archive(const std::string& name, std::string& error) const {
        if (!valid_name(name)) {error="Invalid project name";return false;}
        const auto original=projects_root()/from_utf8(name);
        std::error_code ec;
        if (!fs::is_directory(original,ec) || fs::is_symlink(original,ec)) {
            error="Project entry is unavailable";return false;
        }
        const auto archive_root=projects_root()/".pgr_archived";
        fs::create_directories(archive_root,ec);
        if (ec || fs::is_symlink(archive_root,ec)) {error="Invalid archive location";return false;}
        fs::path destination=archive_root/from_utf8(name);
        for (int i=1; fs::exists(destination,ec) && i<10000; ++i)
            destination=archive_root/from_utf8(name+"_"+std::to_string(i));
        if (fs::exists(destination,ec)) {error="Archive name conflict";return false;}
        fs::rename(original,destination,ec);
        if (ec) {error=ec.message();return false;}
        return true;
    }

    std::vector<project_t> list() const {
        std::vector<project_t> items;
        std::error_code ec;
        for (fs::directory_iterator it(projects_root(), fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_directory() || it->is_symlink() ||
                it->path().filename().u8string().rfind(".",0)==0) continue;
            project_t p;
            std::string error;
            if (read(utf8(it->path().filename()), p, error)) items.push_back(std::move(p));
        }
        std::sort(items.begin(), items.end(), [](const project_t& a, const project_t& b) { return a.name < b.name; });
        return items;
    }

    bool create(const std::string& name, const fs::path& source, project_t& result, std::string& error) const {
        if (!valid_name(name)) { error = "Use 1..80 Latin letters, digits, underscore, dash or dot; avoid Windows reserved names"; return false; }
        if (source.empty()) { error = "Empty source path"; return false; }
        std::error_code ec;
        auto origin = fs::absolute(source, ec).lexically_normal();
        if (ec || origin == origin.root_path()) { error = "Unsafe source path"; return false; }
        auto dir = projects_root() / from_utf8(name);
        if (fs::exists(dir, ec)) { error = "Project already exists: " + name; return false; }
        // Reject nesting the source inside the metadata registry, which would be
        // scanned by build discovery and could recursively include generated files.
        auto registry = fs::weakly_canonical(projects_root(), ec);
        if (ec) { error = ec.message(); return false; }
        auto canonical_origin = fs::weakly_canonical(origin, ec);
        if (ec) { error = ec.message(); return false; }
        auto it = canonical_origin.begin();
        bool inside = true;
        for (auto p = registry.begin(); p != registry.end(); ++p, ++it) {
            if (it == canonical_origin.end() || *it != *p) { inside = false; break; }
        }
        if (inside) { error = "Source directory cannot be inside PGR/Project"; return false; }
        fs::create_directories(origin, ec);
        if (ec) { error = "Cannot create source directory: " + ec.message(); return false; }
        origin = fs::weakly_canonical(origin, ec);
        if (ec || !fs::is_directory(origin)) { error = "Invalid source directory"; return false; }
        if (!fs::create_directory(dir, ec) || ec) { error = "Cannot register project: " + ec.message(); return false; }
        std::ofstream meta(dir / "project.pgr", std::ios::binary | std::ios::trunc);
        meta << "# PGR_make project record v1\nsource=" << utf8(origin) << '\n';
        meta.close();
        if (!meta) { error = "Cannot write project metadata"; fs::remove_all(dir, ec); return false; }
        fs::create_directories(dir / "profiles", ec);
        if (ec) { error = ec.message(); return false; }
        result = {name, origin, dir};
        return true;
    }
};
}
#endif
