#ifndef PGR_DIAGNOSTICS_HPP
#define PGR_DIAGNOSTICS_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include "json_reader.hpp"

// Unified diagnostics for compiler, linker and clangd --check output.
// Raw compiler output is preserved in work.log; JSON is a secondary index.
namespace pgr_diagnostics {
namespace fs = std::filesystem;
struct record {
    std::string severity, category, file, code, message, tool;
    int line = 0, column = 0;
};
inline std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
            } else out += static_cast<char>(c);
        }
    }
    return out;
}
inline std::string category(const std::string& s, bool link = false) {
    if (link) return "linker";
    if (s.find("undefined reference") != std::string::npos ||
        s.find("unresolved external") != std::string::npos ||
        s.find("multiple definition") != std::string::npos) return "linker";
    if (s.find("template") != std::string::npos || s.find("instantiation") != std::string::npos)
        return "template";
    if (s.find("No such file or directory") != std::string::npos ||
        s.find("file not found") != std::string::npos ||
        s.find("cannot open include file") != std::string::npos) return "include";
    if (s.find("not declared") != std::string::npos || s.find("undeclared") != std::string::npos ||
        s.find("not found") != std::string::npos) return "symbol";
    if (s.find("std::") != std::string::npos || s.find("bits/") != std::string::npos)
        return "standard_library";
    if (s.find("syntax") != std::string::npos || s.find("expected") != std::string::npos)
        return "syntax";
    return "compiler";
}
inline bool parse(const std::string& line, record& result) {
    // Greedy filename handles C:\\src\\app.cpp:10:5: ... and Unix paths.
    static const std::regex clangd(R"(^E\[[0-9:.]+\] \[([A-Za-z0-9_]+)\] Line ([0-9]+): (.*)$)");
    static const std::regex gcc_col(R"(^(.+):([0-9]+):([0-9]+):\s*(fatal error|error|warning|note):\s*(.*)$)");
    static const std::regex gcc_line(R"(^(.+):([0-9]+):\s*(fatal error|error|warning|note):\s*(.*)$)");
    static const std::regex msvc(R"(^(.+)\(([0-9]+)(?:,([0-9]+))?\)\s*:\s*(fatal error|error|warning|note)\s+([A-Za-z]+[0-9]+)\s*:\s*(.*)$)");
    static const std::regex msvc_link(R"(^(.+?)\s*:\s*(fatal error|error|warning)\s+(LNK[0-9]+)\s*:\s*(.*)$)");
    static const std::regex linker(R"(^(.*(?:ld|collect2|linker|lld|link\.exe).*?):\s*(.*(?:undefined reference|unresolved|multiple definition|error:).*)$)");
    std::smatch m;
    result = {};
    try {
    if (std::regex_match(line, m, clangd)) {
        result.tool="clangd"; result.severity="error"; result.code=m[1];
        result.line=std::stoi(m[2]); result.message=m[3];
    } else if (std::regex_match(line, m, msvc)) {
        result.file=m[1]; result.line=std::stoi(m[2]); result.column=m[3].matched?std::stoi(m[3]):0;
        result.severity=m[4]; result.code=m[5]; result.message=m[6]; result.tool="MSVC";
    } else if (std::regex_match(line, m, gcc_col)) {
        result.file=m[1]; result.line=std::stoi(m[2]); result.column=std::stoi(m[3]);
        result.severity=m[4]; result.message=m[5]; result.tool="GCC/Clang";
    } else if (std::regex_match(line, m, gcc_line)) {
        result.file=m[1]; result.line=std::stoi(m[2]); result.severity=m[3];
        result.message=m[4]; result.tool="GCC/Clang";
    } else if (std::regex_match(line, m, msvc_link)) {
        result.file=m[1]; result.severity=m[2]; result.code=m[3]; result.message=m[4];
        result.tool="MSVC"; result.category="linker";
    } else if (std::regex_match(line, m, linker)) {
        result.file=m[1]; result.severity="error"; result.message=m[2];
        result.tool="linker"; result.category="linker";
    } else if (line.find("undefined reference to") != std::string::npos ||
               line.find("multiple definition of") != std::string::npos) {
        result.severity="error"; result.message=line; result.tool="linker"; result.category="linker";
    } else return false;
    } catch (const std::exception&) { return false; }
    if (result.code.empty() && result.severity=="warning") {
        static const std::regex warning_code(R"(\[(-W[-A-Za-z0-9_=]+)\]$)");
        std::smatch wc;
        if (std::regex_search(result.message,wc,warning_code)) {
            result.code=wc[1];
            result.message.erase(static_cast<size_t>(wc.position()));
            while (!result.message.empty() && result.message.back()==' ') result.message.pop_back();
        }
    }
    if (result.category.empty()) result.category=category(result.message);
    if (result.severity=="fatal error") result.severity="error";
    return true;
}
class collector {
    std::vector<record> records_;
public:
    void add_line(const std::string& line, const std::string& default_source = {}) {
        record r;
        if (parse(line, r)) {
            if (r.file.empty()) r.file=default_source;
            records_.push_back(std::move(r));
        }
    }
    bool load(const fs::path& path) {
        std::ifstream f(path,std::ios::binary);
        if (!f) return false;
        std::string data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
        if (data.size()>16*1024*1024) return false;
        pgr_json::value root;
        if (!pgr_json::parser(data).parse(root) || root.type!=pgr_json::value::kind::object) return false;
        auto* entries=root.find("diagnostics");
        if (!entries || entries->type!=pgr_json::value::kind::array || entries->array.size()>20000) return false;
        std::vector<record> loaded;
        for (const auto& item:entries->array) {
            if (item.type!=pgr_json::value::kind::object) return false;
            record r;
            auto string_field=[&](const char* name,std::string& dest)->bool {
                const auto* field=item.find(name);
                if (!field || field->type!=pgr_json::value::kind::string) return false;
                dest=field->string;return true;
            };
            auto int_field=[&](const char* name,int& dest)->bool {
                const auto* field=item.find(name);
                if (!field || field->type!=pgr_json::value::kind::number ||
                    field->number<0 || field->number>100000000) return false;
                dest=static_cast<int>(field->number);return true;
            };
            if (!string_field("severity",r.severity) || !string_field("category",r.category) ||
                !string_field("file",r.file) || !string_field("code",r.code) ||
                !string_field("message",r.message) || !string_field("tool",r.tool) ||
                !int_field("line",r.line) || !int_field("column",r.column)) return false;
            loaded.push_back(std::move(r));
        }
        records_=std::move(loaded);
        return true;
    }
    void clear() { records_.clear(); }
    const std::vector<record>& records() const { return records_; }
    int error_count() const {
        return static_cast<int>(std::count_if(records_.begin(), records_.end(),
                 [](const record& d){return d.severity=="error";}));
    }
    bool save(const fs::path& path) const {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << "{\"version\":1,\"diagnostics\":[\n";
        for (size_t i=0; i<records_.size(); ++i) {
            const auto& d=records_[i];
            if (i) f << ",\n";
            f << "{\"severity\":\"" << json_escape(d.severity)
              << "\",\"category\":\"" << json_escape(d.category)
              << "\",\"file\":\"" << json_escape(d.file)
              << "\",\"line\":" << d.line << ",\"column\":" << d.column
              << ",\"code\":\"" << json_escape(d.code)
              << "\",\"message\":\"" << json_escape(d.message)
              << "\",\"tool\":\"" << json_escape(d.tool) << "\"}";
        }
        f << "\n]}\n";
        return static_cast<bool>(f);
    }
    void print(std::ostream& os = std::cout) const {
        os << "Diagnostics: " << records_.size() << " entries, " << error_count() << " errors\n";
        for (size_t i=0; i<records_.size(); ++i) {
            const auto& d=records_[i];
            os << i+1 << ". [" << d.severity << "/" << d.category << "] ";
            if (!d.file.empty()) os << d.file << (d.line>0 ? ":"+std::to_string(d.line) : "")
                   << (d.column>0 ? ":"+std::to_string(d.column) : "") << ": ";
            if (!d.code.empty()) os << d.code << ": ";
            os << d.message << '\n';
        }
    }
};
}
#endif
