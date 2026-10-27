#ifndef PGR_PACK_BULK_HPP
#define PGR_PACK_BULK_HPP
#include <array>
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>
#include "pack_manager.hpp"

namespace pgr_pack_bulk {
using pgr_pack_manager::package;
using pgr_pack_manager::manager;

// Compare numeric version components: 1.13.2 > 1.13.1, not lexical ordering.
// This is intentionally limited to stable versions in our vetted catalog.
inline int compare_version(const std::string& a,const std::string& b) {
    std::size_t ia=0,ib=0;
    while(ia<a.size()||ib<b.size()) {
        unsigned long long va=0,vb=0;
        while(ia<a.size() && !std::isdigit(static_cast<unsigned char>(a[ia]))) ++ia;
        while(ib<b.size() && !std::isdigit(static_cast<unsigned char>(b[ib]))) ++ib;
        while(ia<a.size() && std::isdigit(static_cast<unsigned char>(a[ia]))) {
            va=std::min(1000000000ULL,va*10+static_cast<unsigned>(a[ia++]-'0'));
        }
        while(ib<b.size() && std::isdigit(static_cast<unsigned char>(b[ib]))) {
            vb=std::min(1000000000ULL,vb*10+static_cast<unsigned>(b[ib++]-'0'));
        }
        if(va!=vb) return va>vb?1:-1;
        if(ia==a.size() && ib==b.size()) return 0;
    }
    return 0;
}
inline std::string version_for(const package& p,const std::string& tool) {
    if(p.tool==tool) return p.version;
    for(const auto& extra:p.provides) if(extra.tool==tool) return extra.version;
    return {};
}
inline std::string id_for(const package& p,const std::string& tool) {
    const auto version=version_for(p,tool);
    return version.empty()?std::string{}:tool+"@"+version+"@"+p.platform;
}
// Merge the shipped, curated catalog with Pack/catalog.json. An older Pack
// catalog must not hide newer vetted releases added during a program update.
// A local record with the exact same id intentionally overrides its built-in.
inline std::vector<package> merged_catalog(const pgr_pack_manager::fs::path& root,
                                           const pgr_pack_manager::fs::path& pack,
                                           std::string& error) {
    std::vector<package> combined;
    bool any=false;
    for(const auto& file:{root/"tools"/"catalog.json",pack/"catalog.json"}) {
        if(!pgr_pack_manager::fs::exists(file)) continue;
        any=true;
        std::string diagnostic;
        auto entries=pgr_pack_manager::catalog(file,diagnostic);
        if(!diagnostic.empty()) {error=pgr_workspace::utf8(file)+": "+diagnostic;return {};}
        for(auto& item:entries) {
            auto existing=std::find_if(combined.begin(),combined.end(),[&](const package& p){return p.id==item.id;});
            if(existing!=combined.end()) *existing=std::move(item);
            else combined.push_back(std::move(item));
        }
    }
    if(!any) error="no tool catalog found in tools/ or Pack/";
    return combined;
}
struct plan {
    std::vector<package> downloads;
    std::vector<std::string> already_installed;
    std::vector<std::string> unavailable;
    std::vector<std::string> invalid;
    bool ready() const {return unavailable.empty()&&invalid.empty();}
};
// Core Windows build toolchain: GCC, Clang, CMake and Ninja. A portable
// compiler bundle can provide GCC and Clang from a single download.
inline constexpr std::array<const char*,4> required_tools={{"gcc","clang","cmake","ninja"}};
inline plan latest(const std::vector<package>& catalog,const manager& registry,
                   const std::string& target_platform=pgr_pack_manager::platform()) {
    plan result;
    std::set<std::string> planned;
    for(const char* requirement:required_tools) {
        const std::string tool=requirement;
        const package* best=nullptr;
        std::string version;
        for(const auto& candidate:catalog) {
            const auto v=version_for(candidate,tool);
            if(candidate.platform!=target_platform||v.empty()||
               !pgr_pack_manager::valid_sha(candidate.sha256)||
               candidate.url.rfind("https://",0)!=0) continue;
            if(!best || compare_version(v,version)>0 ||
               (compare_version(v,version)==0 && candidate.id<best->id)) {
                best=&candidate;version=v;
            }
        }
        if(!best) {
            result.unavailable.push_back(tool+": нет проверенного ZIP для "+target_platform);
            continue;
        }
        const auto id=id_for(*best,tool);
        if(const auto* item=registry.find(id)) {
            std::string diagnostic;
            if(!pgr_pack_manager::verify_tool(*item,diagnostic))
                result.invalid.push_back(id+": "+diagnostic);
            else result.already_installed.push_back(id);
        } else if(registry.find(best->id)) {
            // A half-registered bundle must not silently be reinstalled over itself.
            result.invalid.push_back(best->id+": архив уже установлен, но компонент "+tool+" не зарегистрирован");
        } else if(planned.insert(best->id).second) {
            result.downloads.push_back(*best);
        }
    }
    return result;
}
} // namespace pgr_pack_bulk
#endif
