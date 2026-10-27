#include "../core/diagnostics.hpp"
#include "../core/toolchain.hpp"
#undef NDEBUG
#include <cassert>
#include <sstream>
#include <fstream>
int main() {
    using namespace pgr_diagnostics;
    collector c;
    c.add_line(R"(C:\Project\main.cpp(10,5): error C2065: 'thing': undeclared identifier)");
    c.add_line(R"(some.obj : error LNK2019: unresolved external symbol foo)");
    c.add_line(R"(/tmp/main.cpp:4:8: warning: unused variable 'x' [-Wunused-variable])");
    c.add_line(R"(E[16:23:04.125] [undeclared_var_use] Line 22: use of undeclared identifier 'foo')", "/tmp/main.cpp");
    assert(c.records().size() == 4);
    assert(c.records()[0].file == R"(C:\Project\main.cpp)");
    assert(c.records()[0].line == 10 && c.records()[0].column == 5);
    assert(c.records()[0].code == "C2065");
    assert(c.records()[1].category == "linker" && c.records()[1].code == "LNK2019");
    assert(c.records()[2].severity == "warning" && c.records()[2].line == 4);
    assert(c.records()[2].code == "-Wunused-variable");
    assert(c.records()[3].tool == "clangd" && c.records()[3].line == 22);
    assert(c.records()[3].file == "/tmp/main.cpp");
    assert(c.error_count() == 3);
    collector saved;
    saved.add_line(R"(src/main.cpp:7:3: error: expected '{' before '}' token)");
    const auto tempfile=std::filesystem::temp_directory_path()/"pgr_json_diagnostics_unit.json";
    assert(saved.save(tempfile));
    collector reloaded;
    assert(reloaded.load(tempfile));
    assert(reloaded.records().size()==1);
    assert(reloaded.records()[0].message == "expected '{' before '}' token");
    std::error_code ec;
    // Malformed JSON and invalid string escapes must not be treated as valid reports.
    {
        std::ofstream bad(tempfile,std::ios::trunc);
        bad << R"({"diagnostics":[{"severity":"error","message":"\uD800"}]})";
    }
    assert(!reloaded.load(tempfile));
    {
        std::ofstream bad(tempfile,std::ios::trunc);
        bad << R"({"diagnostics":[],"diagnostics":[]})";
    }
    assert(!reloaded.load(tempfile));
    std::filesystem::remove(tempfile,ec);
    assert(pgr_toolchain::classify("C:/tools/MSVC/bin/cl.exe") == pgr_toolchain::family::msvc);
    assert(pgr_toolchain::c_compiler("arm-none-eabi-g++") == "arm-none-eabi-gcc");
    assert(pgr_toolchain::selected_compiler("g++", "riscv64-unknown-elf-") == "riscv64-unknown-elf-g++");
}
