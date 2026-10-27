#!/usr/bin/env python3
"""Deterministic CPU-host acceptance projects. No external packages or network.

Real ARM/RISC-V and GUI tests can be added under tests/fixtures/architectures.
Only projects created inside TemporaryDirectory are modified/cleaned.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(exe, home, *args, success=True):
    proc = subprocess.run([str(exe), '--home', str(home), *args],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace', timeout=100)
    if (proc.returncode == 0) != success:
        raise AssertionError(f"{args}: rc={proc.returncode}\n{proc.stdout[-5000:]}")
    return proc.stdout


def make_project(exe, home, base, name, files):
    source = base / (name + ' source with spaces')
    for relative, code in files.items():
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(code, encoding='utf-8')
    run(exe, home, '--create', name, '--source-dir', str(source))
    return source, home / 'Project' / name / 'config.cfg'


def main(exe):
    with tempfile.TemporaryDirectory(prefix='pgr_release_acceptance_') as work:
        tmp = Path(work)
        home = tmp / 'PGR install'
        home.mkdir()
        # C-only direct build, C++ direct build, native CMake with library,
        # and generated CMake with nested modules all exercise real compilers.
        src, config = make_project(exe, home, tmp, 'c_app', {
            'main.c': 'int main(void){return 0;}\n'
        })
        config.write_text('compiler=gcc\nc_standard=-std=c11\nsource_files=main.c\noutput_file=build/c_app\n',encoding='utf-8')
        run(exe,home,'--project','c_app','--ci','--direct')
        print('PASS c_app direct')

        src, config = make_project(exe, home, tmp, 'cpp_app', {
            'src/main.cpp': '#include <iostream>\nint main(){std::cout<<"ok";}\n'
        })
        config.write_text('compiler=g++\nstandard=-std=c++17\nsource_files=src/main.cpp\n',encoding='utf-8')
        run(exe,home,'--project','cpp_app','--ci')
        print('PASS cpp_app PGR CMake')

        src, config = make_project(exe, home, tmp, 'native_lib', {
            'main.cpp': '#include "lib.h"\nint main(){return answer()!=42;}\n',
            'lib/lib.cpp': 'int answer(){return 42;}\n',
            'lib/lib.h': 'int answer();\n',
            'CMakeLists.txt': '\n'.join([
                'cmake_minimum_required(VERSION 3.16)',
                'project(ExternalFixture LANGUAGES CXX)',
                'add_library(helper STATIC lib/lib.cpp)',
                'target_include_directories(helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)',
                'add_executable(demo main.cpp)',
                'target_link_libraries(demo PRIVATE helper)',
            ]) + '\n'
        })
        original=(src/'CMakeLists.txt').read_bytes()
        run(exe,home,'--project','native_lib','--ci','--native-cmake')
        assert (src/'CMakeLists.txt').read_bytes()==original, 'Original CMake file modified'
        print('PASS native_lib existing CMake unchanged')

        # Real compilation failure must not be reported as success.
        bad, config = make_project(exe, home, tmp, 'invalid', {
            'main.cpp': 'int main() { return nonexistent_identifier; }\n'
        })
        config.write_text('compiler=g++\nsource_files=main.cpp\n',encoding='utf-8')
        run(exe,home,'--project','invalid','--ci',success=False)
        report=home/'Project'/'invalid'/'diagnostics.json'
        assert report.exists() and 'nonexistent_identifier' in report.read_text(errors='replace')
        print('PASS invalid code diagnostics')

        # This deliberately named folder must survive erroneous cleanup attempts.
        valuable=src/'build_user'/'important-user-file.txt'
        valuable.parent.mkdir(parents=True,exist_ok=True)
        valuable.write_text('KEEP ME',encoding='utf-8')
        run(exe,home,'--project','native_lib','--clean-only','--build-dir','build_user',success=False)
        assert valuable.read_text(encoding='utf-8')=='KEEP ME'
        print('PASS unowned directory preserved')

    print('PASS acceptance matrix')


if __name__=='__main__':
    if len(sys.argv)!=2:
        print('Usage: acceptance_matrix.py PATH_TO_PGR_Make',file=sys.stderr)
        sys.exit(2)
    main(Path(sys.argv[1]).resolve())
