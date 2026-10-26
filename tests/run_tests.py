#!/usr/bin/env python3
"""PGR_Make portable workspace integration checks (Python 3.8+).
Usage: python tests/run_tests.py /absolute/path/to/PGR_Make[.exe]
Requires GCC, CMake and preferably Ninja in the test environment.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def check(value, message):
    if not value:
        raise AssertionError(message)


def call(exe, home, *args, cwd=None, input=None, expect_success=True):
    result = subprocess.run([str(exe), "--home", str(home), *args], cwd=cwd,
                            input=input, text=True, encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if expect_success:
        check(result.returncode == 0, "Failed: " + " ".join(args) + "\n" + result.stdout[-4000:])
    else:
        check(result.returncode != 0, "Unexpected success: " + " ".join(args))
    return result


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    exe = Path(sys.argv[1]).resolve()
    check(exe.is_file(), "Executable not found: " + str(exe))
    cases = {
        "cpp": ({"main.cpp": "int main(){return 0;}\n"},
                "compiler=g++\nstandard=-std=c++17\nsource_files=main.cpp\n"),
        "c": ({"main.c": '#include <stdio.h>\nint main(void){puts("C test"); return 0;}\n'},
              "compiler=gcc\nc_standard=-std=c11\nsource_files=main.c\n"),
        "spaces": ({"my file.cpp": "int main(){return 0;}\n"},
                   "compiler=g++\nsource_files=my file.cpp\n"),
        "asan": ({"main.cpp": "int main(){return 0;}\n"},
                 "compiler=g++\nsanitizers=-fsanitize=address\nsource_files=main.cpp\n"),
        "includes": ({"main.cpp": '#include "external.hpp"\nint main(){return f();}\n',
                      "external/include/external.hpp": "inline int f(){return 0;}\n"},
                     "compiler=g++\nsource_files=main.cpp\ninclude_dirs=external/include\n"),
        "finder": ({"main.cpp": "// #finder -Wall\nint main(){return 0;}\n",
                    "lib/comment.hpp": '/* #finder deliberately documented only */\n'},
                   "compiler=g++\nsource_files=main.cpp\n"),
        "header_cycle": ({"main.cpp": '#include "a/a.h"\nint main(){return a();}\n',
                          "a/a.h": '#pragma once\n#include "b/b.h"\ninline int a(){return b();}\n',
                          "b/b.h": '#pragma once\n#include "a/a.h"\ninline int b(){return 0;}\n'},
                         "compiler=g++\nsource_files=main.cpp\n"),
        "shared_library": ({"main.cpp": '#include "mylib/foo.h"\nint main(){return foo();}\n',
                            "mylib/foo.cpp": '#include "foo.h"\nint foo(){return 0;}\n',
                            "mylib/foo.h": '#pragma once\nint foo();\n'},
                           "compiler=g++\nsource_files=main.cpp\nsource_files=mylib/foo.cpp\ntarget_type=shared\n"),
        "gcc_specific": ({"main.cpp": "int main(){return 0;}\n"},
                         "compiler=g++\nsource_files=main.cpp\nspecific_gcc=-Wall\n"),
    }
    with tempfile.TemporaryDirectory(prefix="pgr_workspaces_") as tmp:
        root = Path(tmp)
        home = root / "PGR Install"
        other = root / "outside dir"
        home.mkdir()
        other.mkdir()
        # Nothing is required in the installed program folder besides the exe:
        # Pack and Project should be created automatically.
        for name, (files, config) in cases.items():
            source = other / ("sample " + name)
            source.mkdir()
            for relative, content in files.items():
                file = source / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text(content, encoding="utf-8")
            original_cmake = 'cmake_minimum_required(VERSION 3.16)\n# MUST REMAIN UNCHANGED\n'
            (source / "CMakeLists.txt").write_text(original_cmake, encoding="utf-8")
            call(exe, home, "--create", name, "--source-dir", str(source), cwd=root)
            stored = home / "Project" / name
            check((stored / "project.pgr").exists(), name + ": missing project record")
            check((stored / "config.cfg").exists(), name + ": missing configuration")
            (stored / "config.cfg").write_text(config, encoding="utf-8")
            cmd = ["--project", name, "--ci", "--clean"]
            if name == "spaces":
                cmd += ["--build-dir", "build space"]
            call(exe, home, *cmd, cwd=root)
            check((source / "CMakeLists.txt").read_text(encoding="utf-8") == original_cmake,
                  name + ": original CMakeLists changed")
            stage = stored / "generated_cmake"
            check((stage / "CMakeLists.txt").is_file(), name + ": no generated CMake")
            if name == "shared_library":
                check("SHARED" in (stage / "mylib/CMakeLists.txt").read_text(encoding="utf-8"),
                      "shared library target not generated")
            print("PASS:", name)

        listing = call(exe, home, "--list", cwd=root).stdout
        check("cpp\t" in listing and "c\t" in listing, "project listing failed")
        print("PASS: project selection and registry listing")

        # CI can be run without changing into the source directory.
        source = other / "sample cpp"
        check((source / ".pgr_build" / "cpp").is_dir(), "external source directory not used")
        check(not (home / "build_cmake").exists(), "build leaked into PGR home")
        print("PASS: out-of-directory CI and isolated generated files")

        for bad in (".", "..", "Project", "../outside", "build_cmake/.."):
            call(exe, home, "--project", "cpp", "--ci", "--clean", "--build-dir", bad,
                 cwd=root, expect_success=False)
        print("PASS: dangerous build cleanup rejected")

        cfg = home / "Project" / "cpp" / "config.cfg"
        cfg.write_text("compiler=g++\nsource_files=main.cpp\nwarnings=-Wall;touch_PWNED\n", encoding="utf-8")
        call(exe, home, "--project", "cpp", "--ci", cwd=root, expect_success=False)
        check(not (source / "touch_PWNED").exists(), "shell command was executed")
        print("PASS: shell-injection flag rejected")
        cfg.write_text("# empty config\n", encoding="utf-8")
        call(exe, home, "--project", "cpp", "--ci", cwd=root, expect_success=False)
        print("PASS: empty configuration rejected")

        for invalid in ("../escape", "CON", "bad/name", "..", "bad:name", "LPT1"):
            call(exe, home, "--create", invalid, "--source-dir", str(other / "unsafe"),
                 cwd=root, expect_success=False)
        call(exe, home, "--create", "c", "--source-dir", str(other / "duplicate"),
             cwd=root, expect_success=False)
        print("PASS: invalid names and duplicate names rejected")

        new_src = other / "new starter"
        call(exe, home, "--create", "starter", "--source-dir", str(new_src), "--starter", cwd=root)
        check((new_src / "main.cpp").is_file(), "starter main missing")
        call(exe, home, "--project", "starter", "--ci", cwd=root)
        print("PASS: new project starter created and built")

        unicode_source = other / "исходники пробелы"
        call(exe, home, "--create", "Мой проект", "--source-dir", str(unicode_source),
             "--starter", cwd=root)
        call(exe, home, "--project", "Мой проект", "--ci", cwd=root)
        check((home / "Project" / "Мой проект" / "project.pgr").exists(),
              "Unicode project name not registered")
        print("PASS: Unicode project name and path")

        # A project created interactively should open the project editor immediately.
        interactive_source = other / "interactive new"
        proc = call(exe, home, cwd=root,
                    input="n\ninteractive\n" + str(interactive_source) + "\ny\n0\n")
        check((home / "Project" / "interactive" / "config.cfg").is_file(),
              "interactive project config missing: " + proc.stdout[-800:])
        print("PASS: startup project manager and TUI")

        # Changing between saved projects must not accidentally use old configs.
        before = (home / "Project" / "c" / "config.cfg").read_bytes()
        call(exe, home, "--project", "c", cwd=root, input="0\n")
        check((home / "Project" / "c" / "config.cfg").read_bytes() == before,
              "opening a project changed its config")
        print("PASS: configurations isolated")

        # Named profiles live in the selected project's central metadata folder.
        named_profile = home / "Project" / "starter" / "profiles" / "debug.cfg"
        named_profile.write_text("compiler=g++\nstandard=-std=c++17\n"
                                 "source_files=main.cpp\noptimization=-O0\n", encoding="utf-8")
        call(exe, home, "--project", "starter", "--ci", "--profile", "debug", cwd=root)
        print("PASS: named per-project build profile")

        # Menu item 30 switches between registered projects without restarting EXE.
        # The first two alphabetic entries are c and cpp.
        before = (home / "Project" / "c" / "config.cfg").read_bytes()
        response = call(exe, home, cwd=root, input="1\n30\n2\n0\n")
        check((home / "Project" / "c" / "config.cfg").read_bytes() == before,
              "project switch modified the first project")
        check("Менеджер проектов" in response.stdout, "project manager not restored")
        print("PASS: switching projects in the same process")

        # Direct compiler invocation must execute in external source directory.
        source = other / "new starter"
        call(exe, home, "--project", "starter", cwd=root,
             input="17\ny\n\n0\n")
        suffix = ".exe" if os.name == "nt" else ""
        check((source / "build" / ("starter" + suffix)).exists(),
              "direct compiler build didn't run in project source")
        print("PASS: direct compiler build from external source")

        # Simulate an unpacked toolchain in Pack (not the system PATH).
        if os.name != "nt" and shutil.which("g++"):
            packbin = home / "Pack" / "GCC" / "bin"
            packbin.mkdir(parents=True)
            marker = root / "gcc_pack_marker"
            wrapper = packbin / "g++"
            wrapper.write_text("#!/bin/sh\nprintf x >> '" + str(marker) + "'\nexec " + shutil.which("g++") + " \"$@\"\n", encoding="utf-8")
            wrapper.chmod(0o755)
            call(exe, home, "--project", "starter", "--ci", cwd=root)
            check(marker.is_file(), "Pack compiler was not preferred")
            print("PASS: Pack toolchain is preferred to system PATH")

        # Regression: never delete an unowned directory even if its name resembles build.
        unowned = other / "sample cpp" / "notes" / "build_user"
        unowned.mkdir(parents=True)
        keep = unowned / "important_user_data.txt"
        keep.write_text("Do not delete", encoding="utf-8")
        call(exe, home, "--project", "cpp", "--ci", "--clean",
             "--build-dir", "notes/build_user", cwd=root, expect_success=False)
        check(keep.read_text(encoding="utf-8") == "Do not delete", "unowned files deleted")
        print("PASS: unowned build directory never deleted")

        # A similarly shaped build directory owned by another project is protected.
        same = other / "sample cpp"
        call(exe, home, "--create", "cpp_second", "--source-dir", str(same), cwd=root)
        second_config = home / "Project" / "cpp_second" / "config.cfg"
        second_config.write_text("compiler=g++\nsource_files=main.cpp\n", encoding="utf-8")
        call(exe, home, "--project", "cpp_second", "--ci", cwd=root)
        check((same / ".pgr_build" / "cpp_second" / "CMakeCache.txt").is_file(),
              "shared-source second project didn't build")
        check((same / ".pgr_build" / "cpp" / "CMakeCache.txt").is_file(),
              "shared-source first project build was damaged")
        call(exe, home, "--project", "cpp_second", "--ci", "--clean",
             "--build-dir", str(same / ".pgr_build" / "cpp"),
             cwd=root, expect_success=False)
        print("PASS: multiple project configurations isolated and ownership enforced")

        # Direct C/C++ compilation must compile the .c file as C, not C++.
        mixed_source = other / "mixed C C++"
        mixed_source.mkdir()
        (mixed_source / "clib.c").write_text('int clib(void) { int *restrict p = 0; (void)p; return 0; }\n', encoding="utf-8")
        (mixed_source / "main.cpp").write_text('extern "C" int clib(void);\nint main(){return clib();}\n', encoding="utf-8")
        call(exe, home, "--create", "mixed", "--source-dir", str(mixed_source), cwd=root)
        (home / "Project" / "mixed" / "config.cfg").write_text(
            "compiler=g++\nstandard=-std=c++17\nc_standard=-std=c11\n"
            "source_files=main.cpp\nsource_files=clib.c\noutput_file=build/mixed\n", encoding="utf-8")
        call(exe, home, "--project", "mixed", cwd=root, input="17\ny\n\n0\n")
        check((mixed_source / "build" / ("mixed" + (".exe" if os.name == "nt" else ""))).exists(),
              "direct mixed C/C++ compilation failed")
        call(exe, home, "--project", "mixed", "--ci", cwd=root)
        print("PASS: mixed C and C++ direct and CMake builds")

        # Changing compiler settings must invalidate CMakeCache of PGR-owned folder.
        profile = home / "Project" / "cpp_second" / "config.cfg"
        profile.write_text("compiler=clang++\nsource_files=main.cpp\n", encoding="utf-8")
        if shutil.which("clang++") and shutil.which("clang"):
            call(exe, home, "--project", "cpp_second", "--ci", cwd=root)
            cache_text = (same / ".pgr_build" / "cpp_second" / "CMakeCache.txt").read_text(encoding="utf-8")
            check("clang" in cache_text.lower(), "CMakeCache still uses prior compiler")
            print("PASS: compiler change clears owned CMakeCache")
        else:
            print("SKIP: Clang unavailable for CMakeCache compiler-switch test")
        profile.write_text("compiler=g++\nsource_files=main.cpp\n", encoding="utf-8")

        # An existing source-root compilation database is user data, not disposable output.
        original = same / "compile_commands.json"
        original.write_text("user content", encoding="utf-8")
        call(exe, home, "--project", "cpp_second", "--ci", cwd=root)
        check(original.read_text(encoding="utf-8") == "user content", "existing compile_commands.json overwritten")
        check((home / "Project" / "cpp_second" / "compile_commands.json").exists(),
              "compile_commands.json not preserved in registry")
        print("PASS: compilation database does not overwrite project source data")

        # Native CMake mode uses the user's own CMakeLists without rewriting it.
        native_root = other / "native cmake project"
        native_root.mkdir()
        cmake_text = ('cmake_minimum_required(VERSION 3.16)\n'
                      'project(NativeSample LANGUAGES CXX)\n'
                      'add_executable(native main.cpp)\n')
        (native_root / "CMakeLists.txt").write_text(cmake_text, encoding="utf-8")
        (native_root / "main.cpp").write_text('int main(){return 0;}\n', encoding="utf-8")
        call(exe, home, "--create", "native", "--source-dir", str(native_root), cwd=root)
        call(exe, home, "--project", "native", "--ci", "--native-cmake", cwd=root)
        check((native_root / "CMakeLists.txt").read_text(encoding="utf-8") == cmake_text,
              "native CMakeLists was modified")
        check((native_root / ".pgr_build" / "native" / "CMakeCache.txt").is_file(),
              "native cmake build missing")
        print("PASS: original CMakeLists builds without rewriting")

        # TUI CMake build must regenerate staged project on every configuration change.
        stale_root = other / "sample cpp"
        cfg_tui = home / "Project" / "cpp_second" / "config.cfg"
        cfg_tui.write_text("compiler=g++\nsource_files=main.cpp\noptimization=-O1\n", encoding="utf-8")
        call(exe, home, "--project", "cpp_second", cwd=root, input="19\n\ny\n\n0\n")
        stage = home / "Project" / "cpp_second" / "generated_cmake" / "CMakeLists.txt"
        check('"-O1"' in stage.read_text(encoding="utf-8"), "TUI did not generate -O1")
        cfg_tui.write_text("compiler=g++\nsource_files=main.cpp\noptimization=-O3\n", encoding="utf-8")
        call(exe, home, "--project", "cpp_second", cwd=root, input="19\n\ny\n\n0\n")
        check('"-O3"' in stage.read_text(encoding="utf-8"), "TUI used stale configuration")
        print("PASS: TUI refreshes CMake config before building")

        # Explicit Pack discovery and selection, not just PATH order.
        found = call(exe, home, "--toolchains", cwd=root).stdout
        if os.name != "nt" and shutil.which("g++"):
            check(str(home / "Pack" / "GCC" / "bin" / "g++") in found,
                  "Pack tool listing missing GCC")
            call(exe, home, "--project", "starter", cwd=root, input="31\n1\n0\n")
            saved = (home / "Project" / "starter" / "config.cfg").read_text(encoding="utf-8")
            check(str(home / "Pack" / "GCC" / "bin" / "g++") in saved,
                  "Pack compiler selection not saved")
        print("PASS: explicit Pack compiler listing and selection")

        # A standalone C project should automatically select C compiler.
        purec_source = other / "only nested C project"
        (purec_source / "Sources" / "Application").mkdir(parents=True)
        (purec_source / "Sources" / "Application" / "start.c").write_text(
            "int main(void){return 0;}\n", encoding="utf-8")
        call(exe, home, "--create", "purec", "--source-dir", str(purec_source), cwd=root)
        generated_cfg = (home / "Project" / "purec" / "config.cfg").read_text(encoding="utf-8")
        check("compiler=gcc" in generated_cfg and "source_files=Sources/Application/start.c" in generated_cfg,
              "recursive C source autodiscovery failed")
        call(exe, home, "--project", "purec", "--ci", cwd=root)
        print("PASS: recursive C source discovery and proper compiler")

    print("All regression tests passed.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print("FAIL:", exc, file=sys.stderr)
        sys.exit(1)
