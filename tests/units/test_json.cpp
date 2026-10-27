#include "common.hpp"
#include "core/json_reader.hpp"
using namespace pgr_test;
int main() { try {
    pgr_json::value v;
    check(pgr_json::parser(R"({"ok":true,"n":-12,"name":"\u041f\u0440\u043e\u0435\u043a\u0442","arr":[null,false,"x"]})").parse(v),"valid JSON");
    check(v.find("n") && v.find("n")->number==-12,"negative integer");
    check(v.find("name") && v.find("name")->string=="Проект","Unicode escape");
    for(const std::string malformed : {
        R"({"a":1,"a":2})",R"({"n":01})",R"({"a":1,})",R"({"a":"\uD800"})",
        R"({"a":1}garbage)",R"({"a":1.3})", R"({"a":"line
feed"})",
        "[1,,2]", "{", "-", "1e999999"}) {
        pgr_json::value x;
        check(!pgr_json::parser(malformed).parse(x),"accepted invalid JSON: "+malformed);
    }
    // Bounded recursion prevents stack exhaustion.
    std::string nested(40,'[');nested+="0";nested+=std::string(40,']');
    pgr_json::value x;
    check(!pgr_json::parser(nested).parse(x),"unbounded JSON recursion");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
