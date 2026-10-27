#include "common.hpp"
#include "core/pack_manager.hpp"
using namespace pgr_test;
int main() { try {
    sandbox t("pack");
    using namespace pgr_pack_manager;
    check(safe_id("gcc@14.2.0@windows-x86_64"),"valid package id");
    for (auto id : {"../bad", "", ".", "x/y", "x\\y"}) check(!safe_id(id),"accepted dangerous package id");
    check(safe_relative("bin/cmake.exe"),"valid relative executable");
    for (auto rel : {"../evil.exe", "/tmp/evil", "a/../evil", "./bad", "C:evil"})
        check(!safe_relative(rel),std::string("accepted unsafe executable path: ")+rel);
    check(!valid_sha("01234"),"accepted short checksum");
    check(valid_sha(std::string(64,'a')),"valid hex SHA-256");
    package p;
    pgr_json::value obj;
    check(pgr_json::parser(R"({"id":"cmake@1","tool":"cmake","version":"1","platform":"linux","url":"https://example.org/test.zip","sha256":"","executable":"bin/cmake"})").parse(obj),"package fixture JSON");
    check(parse_package(obj,p),"valid package schema");
    put(t.path/"catalog.json",R"({"packages":[{"id":"cmake@1","tool":"cmake","version":"1","platform":"linux","url":"https://example.org/test.zip","sha256":"","executable":"bin/cmake"},{"id":"cmake@1","tool":"cmake","version":"2","platform":"linux","url":"","sha256":"","executable":"bin/cmake"}]})");
    std::string error;
    check(catalog(t.path/"catalog.json",error).empty() && !error.empty(),"duplicate package accepted");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
