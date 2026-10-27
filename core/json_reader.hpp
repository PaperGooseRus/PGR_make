#ifndef PGR_JSON_READER_HPP
#define PGR_JSON_READER_HPP
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

// Small strict JSON reader with bounded recursion. Used only for PGR's own
// diagnostics.json schema; not a replacement for a full JSON ecosystem.
namespace pgr_json {
struct value {
    enum class kind { null_value, boolean, number, string, array, object } type=kind::null_value;
    bool boolean=false;
    std::int64_t number=0;
    std::string string;
    std::vector<value> array;
    std::map<std::string,value> object;
    const value* find(const std::string& key) const {
        const auto it=object.find(key);
        return it==object.end()?nullptr:&it->second;
    }
};
class parser {
    const std::string& source;
    std::size_t pos=0;
    void spaces() {
        while (pos<source.size() && (source[pos]==' ' || source[pos]=='\t' ||
               source[pos]=='\n' || source[pos]=='\r')) ++pos;
    }
    bool char_is(char c) {spaces();if (pos<source.size() && source[pos]==c) {++pos;return true;}return false;}
    static bool hex(char c,unsigned& v) {
        if (c>='0' && c<='9') v=c-'0';
        else if (c>='a' && c<='f') v=c-'a'+10;
        else if (c>='A' && c<='F') v=c-'A'+10;
        else return false;
        return true;
    }
    bool unicode(unsigned& out) {
        if (pos+4>source.size()) return false;
        out=0;
        for (int k=0;k<4;++k) {
            unsigned v=0;
            if (!hex(source[pos++],v)) return false;
            out=(out<<4)|v;
        }
        return true;
    }
    static void utf8(std::string& target,unsigned cp) {
        if (cp<=0x7F) target+=static_cast<char>(cp);
        else if (cp<=0x7FF) {target+=char(0xC0|(cp>>6));target+=char(0x80|(cp&0x3F));}
        else if (cp<=0xFFFF) {target+=char(0xE0|(cp>>12));target+=char(0x80|((cp>>6)&0x3F));target+=char(0x80|(cp&0x3F));}
        else {target+=char(0xF0|(cp>>18));target+=char(0x80|((cp>>12)&0x3F));target+=char(0x80|((cp>>6)&0x3F));target+=char(0x80|(cp&0x3F));}
    }
    bool string_value(std::string& out) {
        if (!char_is('"')) return false;
        for (;pos<source.size();) {
            unsigned char c=static_cast<unsigned char>(source[pos++]);
            if (c=='"') return true;
            if (c<0x20) return false;
            if (c!='\\') {out+=static_cast<char>(c);continue;}
            if (pos>=source.size()) return false;
            switch (const char esc=source[pos++]) {
                case '"': out+='"';break;
                case '\\': out+='\\';break;
                case '/': out+='/';break;
                case 'b': out+='\b';break;
                case 'f': out+='\f';break;
                case 'n': out+='\n';break;
                case 'r': out+='\r';break;
                case 't': out+='\t';break;
                case 'u': {
                    unsigned cp=0;
                    if (!unicode(cp)) return false;
                    if (cp>=0xD800 && cp<=0xDBFF) {
                        if (pos+2>source.size() || source[pos++]!='\\' || source[pos++]!='u') return false;
                        unsigned lo=0;
                        if (!unicode(lo) || lo<0xDC00 || lo>0xDFFF) return false;
                        cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00);
                    } else if (cp>=0xDC00 && cp<=0xDFFF) return false;
                    utf8(out,cp);break;
                }
                default: (void)esc;return false;
            }
        }
        return false;
    }
    bool number_value(value& out) {
        const auto begin=pos;
        if (source[pos]=='-') ++pos;
        if (pos==source.size()) return false;
        if (source[pos]=='0') ++pos;
        else if (source[pos]>='1' && source[pos]<='9') {
            do {++pos;} while(pos<source.size() && source[pos]>='0' && source[pos]<='9');
        } else return false;
        // Diagnostic positions must be integers; JSON fractional/exponent numbers
        // are rejected instead of silently truncated.
        if (pos<source.size() && (source[pos]=='.'||source[pos]=='e'||source[pos]=='E')) return false;
        try {
            out.number=std::stoll(source.substr(begin,pos-begin));
        } catch (...) {return false;}
        out.type=value::kind::number;
        return true;
    }
    bool parse_value(value& out,int depth) {
        spaces();
        if (depth>32 || pos>=source.size()) return false;
        if (source[pos]=='"') {out.type=value::kind::string;return string_value(out.string);}
        if (source[pos]=='{' ) {
            ++pos;out.type=value::kind::object;
            if (char_is('}')) return true;
            for (;;) {
                std::string key;
                if (!string_value(key)||!char_is(':')) return false;
                value child;
                if (!parse_value(child,depth+1) || out.object.count(key)) return false;
                out.object.emplace(std::move(key),std::move(child));
                if (char_is('}')) return true;
                if (!char_is(',')) return false;
            }
        }
        if (source[pos]=='[') {
            ++pos;out.type=value::kind::array;
            if (char_is(']')) return true;
            for (;;) {
                if (out.array.size()>100000) return false;
                value child;
                if (!parse_value(child,depth+1)) return false;
                out.array.emplace_back(std::move(child));
                if (char_is(']')) return true;
                if (!char_is(',')) return false;
            }
        }
        for (auto& constant : {std::pair<const char*,value::kind>{"true",value::kind::boolean},
                               {"false",value::kind::boolean},{"null",value::kind::null_value}}) {
            const std::string pattern=constant.first;
            if (source.compare(pos,pattern.size(),pattern)==0) {
                pos+=pattern.size();out.type=constant.second;
                out.boolean=pattern=="true";return true;
            }
        }
        if (source[pos]=='-' || (source[pos]>='0'&&source[pos]<='9')) return number_value(out);
        return false;
    }
public:
    explicit parser(const std::string& input):source(input){}
    bool parse(value& result) {return parse_value(result,0) && (spaces(),pos==source.size());}
};
}
#endif
