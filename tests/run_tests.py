#!/usr/bin/env python3
"""Functional regression tests for PGR_make CLI. Python 3.8+.

Usage: python tests/run_tests.py /absolute/path/to/pgr_make[.exe]
Build the executable with CMake or the platform C++17 compiler first.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def check(condition, explanation):
    if not condition:
        raise AssertionError(explanation)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    executable = Path(sys.argv[1]).resolve()
    check(executable.is_file(), "executable does not exist: " + str(executable))

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
    with tempfile.TemporaryDirectory(prefix="pgr_make_tests_") as temp:
        root = Path(temp)
        for name, (files, config) in cases.items():
            wd = root / name
            (wd / "project").mkdir(parents=True)
            for relative, content in files.items():
                p = wd / relative
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content, encoding="utf-8")
            (wd / "project/config.cfg").write_text(config, encoding="utf-8")
            original_cmake = 'cmake_minimum_required(VERSION 3.16)\n# MUST REMAIN UNCHANGED\n'
            (wd / "CMakeLists.txt").write_text(original_cmake, encoding="utf-8")
            args = [str(executable), "--ci", "--clean"]
            if name == "spaces":
                args += ["--build-dir", "build space"]
            result = subprocess.run(args, cwd=wd, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            check(result.returncode == 0, name + " failed:\n" + result.stdout[-4000:])
            check((wd / "CMakeLists.txt").read_text(encoding="utf-8") == original_cmake,
                  name + ": CI modified the original CMakeLists.txt")
            stage = wd / "project/generated_cmake"
            check((stage / "CMakeLists.txt").is_file(), name + ": no generated project")
            if name == "shared_library":
                check('SHARED' in (stage / "mylib/CMakeLists.txt").read_text(encoding="utf-8"),
                      "shared library target not generated")
            print("PASS:", name)

        wd = root / "cpp"
        for bad in (".", "..", "project", "../outside", "build_cmake/.."):
            proc = subprocess.run([str(executable), "--ci", "--clean", "--build-dir", bad],
                                  cwd=wd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True)
            check(proc.returncode != 0, "dangerous clean accepted: " + bad)
        print("PASS: dangerous directory cleanup rejected")

        malicious = wd / "project/config.cfg"
        malicious.write_text("compiler=g++\nsource_files=main.cpp\nwarnings=-Wall;touch_PWNED\n",
                             encoding="utf-8")
        proc = subprocess.run([str(executable), "--ci", "--build-dir", "build_injection"],
                              cwd=wd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True)
        check(proc.returncode != 0, "malicious flag accepted")
        check(not (wd / "touch_PWNED").exists(), "shell command was executed")
        print("PASS: invalid compiler switch rejected")

        malicious.write_text("# empty config\n", encoding="utf-8")
        proc = subprocess.run([str(executable), "--ci"], cwd=wd,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        check(proc.returncode != 0, "empty config accepted")
        print("PASS: empty config rejected")

    print("All regression tests passed.")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print("FAIL:", error, file=sys.stderr)
        sys.exit(1)
