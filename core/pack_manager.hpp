#ifndef PGR_PACK_MANAGER_HPP
#define PGR_PACK_MANAGER_HPP
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "json_reader.hpp"
#include "process_runner.hpp"
#include "sha256.hpp"
#include "workspace.hpp"
#include "toolchain.hpp"

namespace pgr_pack_manager {
namespace fs=std::filesystem;
struct package {
    std::string id,tool,version,platform,url,sha256,executable;
};
struct installed : package {
    fs::path root;
    bool external=false;
    std::string binary_sha256;
};
inline std::string str(const pgr_json::value& o,const char* key) {
    const auto* v=o.find(key);
    return v && v->type==pgr_json::value::kind::string?v->string:std::string{};
}
inline std::string json_string(const std::string& s) {
    std::string out="\"";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c=='\n') out+="\\n";
        else if(c=='\r') out+="\\r";
        else if(c=='\t') out+="\\t";
        else if(c<32) {const char* hex="0123456789abcdef";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
inline bool safe_id(const std::string& id) {
    return !id.empty() && id!="." && id!=".." && id.front()!='.' && id.size()<=128 && std::all_of(id.begin(),id.end(),[](unsigned char c){
        return std::isalnum(c)||c=='_'||c=='-'||c=='.'||c=='@';});
}
inline bool valid_sha(const std::string& value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isxdigit(c)!=0;});
}
inline std::string lowercase(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return s;
}
inline bool safe_relative(const fs::path& path) {
    if(path.empty()||path.is_absolute()||path.has_root_name()||path.has_root_directory()) return false;
    for(const auto& part:path) if(part==".."||part=="."||part.empty()) return false;
    const auto s=path.generic_u8string();
    return s.find(':')==std::string::npos && s.find('\\')==std::string::npos;
}
inline bool read_json(const fs::path& path,pgr_json::value& value) {
    std::ifstream input(path,std::ios::binary);
    if(!input) return false;
    std::string text((std::istreambuf_iterator<char>(input)),{});
    if(text.size()>2*1024*1024) return false;
    return pgr_json::parser(text).parse(value) && value.type==pgr_json::value::kind::object;
}
inline bool parse_package(const pgr_json::value& value,package& pkg) {
    if(value.type!=pgr_json::value::kind::object) return false;
    pkg={str(value,"id"),str(value,"tool"),str(value,"version"),str(value,"platform"),
         str(value,"url"),lowercase(str(value,"sha256")),str(value,"executable")};
    return safe_id(pkg.id)&&safe_id(pkg.tool)&&safe_id(pkg.version)&&safe_id(pkg.platform)&&
        safe_relative(fs::u8path(pkg.executable)) &&
        (pkg.sha256.empty()||valid_sha(pkg.sha256));
}
inline std::vector<package> catalog(const fs::path& file,std::string& error) {
    std::vector<package> result;
    pgr_json::value root;
    if(!read_json(file,root)) {error="catalog JSON unavailable or malformed";return result;}
    const auto* arr=root.find("packages");
    if(!arr||arr->type!=pgr_json::value::kind::array) {error="catalog: missing packages";return {};}
    std::set<std::string> seen;
    for(const auto& v:arr->array) {
        package p;
        if(!parse_package(v,p)||!seen.insert(p.id).second) {error="catalog has invalid/duplicate entry";return {};}
        result.push_back(std::move(p));
    }
    return result;
}
inline std::string platform() {
#ifdef _WIN32
#if defined(_M_ARM64) || defined(__aarch64__)
    return "windows-arm64";
#elif defined(_WIN64)
    return "windows-x86_64";
#else
    return "windows-x86";
#endif
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}
inline fs::path executable_path(const installed& item) {
    if(!safe_relative(fs::u8path(item.executable))) return {};
    return item.root/fs::u8path(item.executable);
}
inline bool contains_link(const fs::path& dir) {
    std::error_code ec;
    if(fs::is_symlink(dir,ec)) return true;
    if(!fs::exists(dir,ec)) return false;
    for(fs::recursive_directory_iterator it(dir,fs::directory_options::skip_permission_denied,ec),end;
        !ec && it!=end;it.increment(ec)) {
        if(it->is_symlink(ec)) return true;
    }
    return ec.operator bool();
}
inline bool verify_tool(const installed& item,std::string& error) {
    const auto executable=executable_path(item);
    std::error_code ec;
    if(executable.empty()||!fs::is_regular_file(executable,ec)||fs::is_symlink(executable,ec)) {
        error="tool executable is missing or symlinked";return false;
    }
    std::string first;
    pgr_process::options opt;opt.quiet=true;
    opt.output=[&](const std::string& line){if(first.empty()) first=line;};
    const auto rc=pgr_process::run_ex({pgr_workspace::utf8(executable),item.tool=="msvc"?"/Bv":"--version"},opt);
    if(rc!=0 || first.find(item.version)==std::string::npos) {
        error="executable version check failed (expected "+item.version+", got: "+first+")";return false;
    }
    if(!item.binary_sha256.empty() && lowercase(pgr_sha256::file(executable))!=lowercase(item.binary_sha256)) {
        error="installed executable has changed (SHA-256 mismatch)";return false;
    }
    if(item.tool=="gcc" || item.tool=="clang") {
        // Explicitly imported compiler bundles may include their dependencies.
        // Verify the companion C compiler, never assume GCC is one executable.
        const auto cc=pgr_toolchain::c_compiler(pgr_workspace::utf8(executable));
        const fs::path companion=fs::u8path(cc);
        if(cc.empty() || companion==executable || !fs::is_regular_file(companion,ec)) {
            error="incomplete compiler bundle: missing companion C compiler";return false;
        }
    }
    return true;
}
inline std::string serialize(const installed& x) {
    return "{\"id\":"+json_string(x.id)+",\"tool\":"+json_string(x.tool)+
           ",\"version\":"+json_string(x.version)+",\"platform\":"+json_string(x.platform)+
           ",\"url\":"+json_string(x.url)+",\"sha256\":"+json_string(x.sha256)+
           ",\"executable\":"+json_string(x.executable)+",\"root\":"+
           json_string(pgr_workspace::utf8(x.root)) + ",\"external\":"+(x.external?"true":"false")+
           ",\"binary_sha256\":"+json_string(x.binary_sha256)+"}";
}
class manager {
    fs::path pack_,script_root_;
    std::vector<installed> records_;
public:
    explicit manager(fs::path pack,fs::path scripts={}) :pack_(std::move(pack)),script_root_(std::move(scripts)) {}
    fs::path registry_file() const{return pack_/"registry.json";}
    fs::path catalog_file() const {
        const auto custom=pack_/"catalog.json";
        return fs::exists(custom)?custom:script_root_/"tools"/"catalog.json";
    }
    const std::vector<installed>& entries() const{return records_;}
    const installed* find(const std::string& id) const {
        for(const auto& item:records_) if(item.id==id) return &item;
        return nullptr;
    }
    bool load(std::string& error) {
        records_.clear();
        if(!fs::exists(registry_file())) return true;
        pgr_json::value root;
        if(!read_json(registry_file(),root)) {error="invalid Pack/registry.json";return false;}
        const auto* items=root.find("installed");
        if(!items||items->type!=pgr_json::value::kind::array) {error="Pack registry missing installed array";return false;}
        std::set<std::string> seen;
        for(const auto& value:items->array) {
            installed item;
            if(!parse_package(value,item)||!seen.insert(item.id).second) {
                error="invalid/duplicate installed entry";return false;
            }
            item.root=fs::u8path(str(value,"root"));
            item.binary_sha256=lowercase(str(value,"binary_sha256"));
            if(!item.binary_sha256.empty()&&!valid_sha(item.binary_sha256)) {error="invalid binary SHA-256";return false;}
            const auto* ext=value.find("external");
            item.external=ext && ext->type==pgr_json::value::kind::boolean && ext->boolean;
            if(!item.root.is_absolute()) {error="registry path must be absolute";return false;}
            if(!item.external) {
                std::error_code ec;
                auto relative=item.root.lexically_relative(pack_);
                if(!safe_relative(relative)||relative=="Downloads"||relative=="Temp") {
                    error="registry has entry outside Pack";return false;
                }
                // Symlink paths are never trusted to refer to managed installs.
                if(fs::exists(item.root,ec) && fs::is_symlink(item.root,ec)) {
                    error="registry points to symlink";return false;
                }
            }
            records_.push_back(std::move(item));
        }
        return true;
    }
    bool save(std::string& error) const {
        std::error_code ec;
        fs::create_directories(pack_,ec);
        if(ec){error=ec.message();return false;}
        const auto tempfile=pack_/"registry.json.tmp";
        if(fs::exists(tempfile)) {error="temporary registry file exists";return false;}
        std::ofstream out(tempfile,std::ios::binary);
        if(!out){error="cannot open registry temp file";return false;}
        out<<"{\"schema\":1,\"installed\":[";
        for(std::size_t i=0;i<records_.size();++i) {if(i)out<<',';out<<serialize(records_[i]);}
        out<<"]}\n";out.close();
        if(!out){fs::remove(tempfile,ec);error="registry write failed";return false;}
#ifdef _WIN32
        // Windows std::filesystem::rename does not overwrite. Preserve old on failure.
        const auto backup=pack_/"registry.json.bak";
        fs::remove(backup,ec);ec.clear();
        if(fs::exists(registry_file())) fs::rename(registry_file(),backup,ec);
        if(ec){fs::remove(tempfile);error="cannot backup registry";return false;}
        fs::rename(tempfile,registry_file(),ec);
        if(ec && fs::exists(backup)) fs::rename(backup,registry_file());
        else fs::remove(backup,ec);
#else
        fs::rename(tempfile,registry_file(),ec);
#endif
        if(ec){error="registry commit failed: "+ec.message();return false;}
        return true;
    }
    bool register_external(installed item,std::string& error) {
        if(!safe_id(item.id)||!safe_id(item.tool)||!safe_id(item.version)||!safe_relative(fs::u8path(item.executable))||find(item.id)) {
            error="invalid tool metadata or duplicate id";return false;
        }
        if(!item.root.is_absolute()||!fs::is_directory(item.root)) {error="external root must exist";return false;}
        item.external=true;
        if(!verify_tool(item,error)) return false;
        item.binary_sha256=pgr_sha256::file(executable_path(item));
        records_.push_back(std::move(item));
        if(!save(error)) {records_.pop_back();return false;}
        return true;
    }
    bool remove(const std::string& id,const std::vector<std::string>& references,std::string& error) {
        auto iter=std::find_if(records_.begin(),records_.end(),[&](const installed& a){return a.id==id;});
        if(iter==records_.end()){error="unknown tool id";return false;}
        if(std::find(references.begin(),references.end(),id)!=references.end()) {
            error="tool is referenced by a project or profile";return false;
        }
        // No deletion of actual distributions. Managed installation is quarantined,
        // never recursively deleted by the manager.
        if(!iter->external) {
            const auto dest=pack_/"Retired"/fs::u8path(id);
            std::error_code ec;
            if(fs::exists(dest)){error="quarantine destination exists";return false;}
            fs::create_directories(dest.parent_path(),ec);
            if(ec){error=ec.message();return false;}
            fs::rename(iter->root,dest,ec);
            if(ec){error="unable to retire active tool: "+ec.message();return false;}
        }
        const auto original=records_;
        records_.erase(iter);
        if(!save(error)) {
            records_=original;
            if(!original.empty()) {
                const auto& rec=*std::find_if(original.begin(),original.end(),[&](const installed& entry){return entry.id==id;});
                if(!rec.external) {
                    std::error_code ec;
                    fs::rename(pack_/"Retired"/fs::u8path(id),rec.root,ec);
                }
            }
            return false;
        }
        return true;
    }
    bool install(package pkg,const fs::path& local_archive,std::string& error) {
        if(!safe_id(pkg.id)||!safe_id(pkg.tool)||!safe_id(pkg.version)||!safe_id(pkg.platform)||
           !safe_relative(fs::u8path(pkg.executable))||!valid_sha(pkg.sha256)||find(pkg.id)) {
            error="invalid package, missing SHA-256, or version already installed";return false;
        }
        if(pkg.platform!=platform()) {error="archive is for a different platform: "+pkg.platform;return false;}
        if(!local_archive.empty() && !fs::is_regular_file(local_archive)) {error="ZIP archive not found";return false;}
        if(local_archive.empty() && (pkg.url.rfind("https://",0)!=0 || pkg.url.find_first_of("\r\n\t")!=std::string::npos)) {
            error="download only from explicit HTTPS catalogue URL";return false;
        }
        std::error_code ec;
        const fs::path final=pack_/fs::u8path(pkg.tool)/fs::u8path(pkg.version+"-"+pkg.platform);
        const fs::path staging=pack_/"Temp"/fs::u8path(pkg.id+".installing");
        if(fs::exists(final)||fs::exists(staging)) {error="target or pending staging already exists";return false;}
        if(contains_link(pack_ / fs::u8path(pkg.tool))) {error="tool directory contains symlink";return false;}
        if(fs::is_symlink(pack_/"Temp",ec)) {error="Temp symlink forbidden";return false;}
        fs::create_directories(staging.parent_path(),ec);
        if(ec || !fs::create_directory(staging,ec)) {
            error="another installation is running or staging directory is unavailable";return false;
        }
        struct cleanup {fs::path path;~cleanup(){std::error_code ec;fs::remove_all(path,ec);}} rollback{staging};
        fs::path archive=local_archive;
        if(archive.empty()) {
            archive=staging/"package.zip";
            if(pgr_process::run({"curl","--fail","--location","--proto","=https","--proto-redir","=https","--tlsv1.2",
                                 "--max-time","240","--output",pgr_workspace::utf8(archive),pkg.url},true)!=0) {
                error="HTTPS download failed; install curl or import a local archive";return false;
            }
        }
        if(lowercase(pgr_sha256::file(archive))!=lowercase(pkg.sha256)) {
            error="SHA-256 mismatch: download/import rejected";return false;
        }
        const fs::path extracted=staging/"unpacked";
        fs::create_directories(extracted,ec);
        if(ec){error=ec.message();return false;}
#ifdef _WIN32
        const auto script=script_root_/"tools"/"zip_guard.ps1";
        const int extract_rc=pgr_process::run({"powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass",
                                              "-File",pgr_workspace::utf8(script),pgr_workspace::utf8(archive),pgr_workspace::utf8(extracted)},true);
#else
        const auto script=script_root_/"tools"/"zip_guard.py";
        const int extract_rc=pgr_process::run({"python3",pgr_workspace::utf8(script),
                                              pgr_workspace::utf8(archive),pgr_workspace::utf8(extracted)},true);
#endif
        if(extract_rc!=0 || contains_link(extracted)) {error="unsafe ZIP or extraction failed";return false;}
        // Accept archives with a single containing directory (e.g., Kitware CMake).
        fs::path content=extracted;
        if(!fs::exists(content/fs::u8path(pkg.executable))) {
            fs::path only;
            int entries=0;
            for(auto& child:fs::directory_iterator(content)) {++entries;only=child.path();}
            if(entries==1 && fs::is_directory(only) && !fs::is_symlink(only)) content=only;
        }
        installed record;
        static_cast<package&>(record)=pkg;
        record.root=fs::absolute(final).lexically_normal();
        record.external=false;
        const auto relative=fs::u8path(pkg.executable);
        if(!fs::is_regular_file(content/relative)) {error="expected executable missing in archive";return false;}
#ifndef _WIN32
        auto perms=fs::status(content/relative,ec).permissions();
        if(!ec) fs::permissions(content/relative,perms|fs::perms::owner_exec|fs::perms::group_exec|fs::perms::others_exec,ec);
#endif
        installed staged=record;staged.root=content;
        if(!verify_tool(staged,error)) return false;
        record.binary_sha256=pgr_sha256::file(content/relative);
        if(!valid_sha(record.binary_sha256)) {error="cannot hash installed executable";return false;}
        fs::create_directories(final.parent_path(),ec);
        if(ec){error=ec.message();return false;}
        fs::rename(content,final,ec);
        if(ec){error="unable to move verified tool into Pack: "+ec.message();return false;}
        records_.push_back(record);
        if(!save(error)) {
            records_.pop_back();
            // Do not destroy the installed directory after a registry failure.
            return false;
        }
        return true;
    }
};
}
#endif
