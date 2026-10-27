#!/usr/bin/env python3
"""Expanded regression tests for diagnostics, toolchains, presets and clangd check."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def invoke(exe, home, *args, success=True, inp=None):
    p = subprocess.run([str(exe), '--home', str(home), *args], input=inp,
                       text=True, encoding='utf-8', errors='replace',
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if (p.returncode == 0) != success:
        raise AssertionError(f"{args} code={p.returncode}\n{p.stdout[-3500:]}")
    return p.stdout


def main(exe):
    with tempfile.TemporaryDirectory(prefix='pgr_features_') as d:
        tmp = Path(d)
        home = tmp/'PGR'; home.mkdir()
        src = tmp/'external source'; src.mkdir()
        (src/'main.cpp').write_text('int main(){return 0;}\n', encoding='utf8')
        invoke(exe,home,'--create','app','--source-dir',str(src))
        cfg=home/'Project'/'app'/'config.cfg'
        cfg.write_text('compiler=g++\nsource_files=main.cpp\noptimization=-O0\n',encoding='utf8')
        invoke(exe,home,'--project','app','--write-presets')
        stage=home/'Project'/'app'/'generated_cmake'
        assert json.loads((stage/'CMakePresets.json').read_text())['version']==3
        p=subprocess.run(['cmake','--list-presets'],cwd=stage,stdout=subprocess.PIPE,text=True)
        assert p.returncode==0 and 'pgr-default' in p.stdout, p.stdout
        print('PASS: isolated CMakePresets.json and CMake preset validation')
        cfg.write_text('compiler=g++\nsource_files=main.cpp\npackage_manager=conan\n',encoding='utf8')
        invoke(exe,home,'--project','app','--write-presets',success=False)
        print('PASS: Conan requires generated toolchain, no fictitious install')
        cfg.write_text('compiler=g++\nsource_files=main.cpp\npackage_manager=vcpkg\n',encoding='utf8')
        old=os.environ.pop('VCPKG_ROOT',None)
        try:
            invoke(exe,home,'--project','app','--write-presets',success=False)
        finally:
            if old is not None: os.environ['VCPKG_ROOT']=old
        print('PASS: vcpkg without VCPKG_ROOT fails with useful error')
        fake=tmp/'fake vcpkg'/'scripts'/'buildsystems';fake.mkdir(parents=True)
        (fake/'vcpkg.cmake').write_text('# fake test toolchain\n')
        os.environ['VCPKG_ROOT']=str(tmp/'fake vcpkg')
        invoke(exe,home,'--project','app','--write-presets')
        presets=json.loads((stage/'CMakePresets.json').read_text())
        assert presets['configurePresets'][0]['cacheVariables']['CMAKE_TOOLCHAIN_FILE'].startswith('$env{VCPKG_ROOT}')
        os.environ.pop('VCPKG_ROOT',None)
        print('PASS: vcpkg preset resolves VCPKG_ROOT')
        cfg.write_text('compiler=g++\nsource_files=main.cpp\n',encoding='utf8')
        invoke(exe,home,'--project','app','--ci')
        diag=json.loads(invoke(exe,home,'--project','app','--diagnostics'))
        assert diag['version']==1 and diag['diagnostics']==[]
        print('PASS: successful compilation creates empty diagnostics array')
        (src/'main.cpp').write_text('int main(){return does_not_exist;}\n',encoding='utf8')
        invoke(exe,home,'--project','app','--ci',success=False)
        diag=json.loads(invoke(exe,home,'--project','app','--diagnostics'))
        assert len(diag['diagnostics'])>0,diag
        assert any(x['line'] == 1 and x['severity']=='error' for x in diag['diagnostics']),diag
        print('PASS: GCC compile failures parsed and saved as structured JSON')
        (src/'main.cpp').write_text('extern int missing_function();\nint main(){return missing_function();}\n',encoding='utf8')
        invoke(exe,home,'--project','app','--ci',success=False)
        diag=json.loads(invoke(exe,home,'--project','app','--diagnostics'))
        assert any(x['category']=='linker' for x in diag['diagnostics']),diag
        print('PASS: linker failures categorized separately')
        libsrc=tmp/'object sources'; libsrc.mkdir()
        (libsrc/'main.cpp').write_text('int value(); int main(){return value();}\n',encoding='utf8')
        (libsrc/'mylib').mkdir()
        (libsrc/'mylib'/'value.cpp').write_text('int value(){return 0;}\n',encoding='utf8')
        (libsrc/'mylib'/'value.h').write_text('#pragma once\nint value();\n',encoding='utf8')
        # Detect dependency through include, rather than relying on directory names.
        (libsrc/'main.cpp').write_text('#include "mylib/value.h"\nint main(){return value();}\n',encoding='utf8')
        invoke(exe,home,'--create','object','--source-dir',str(libsrc))
        (home/'Project'/'object'/'config.cfg').write_text(
            'compiler=g++\nsource_files=main.cpp\nsource_files=mylib/value.cpp\ntarget_type=object\n',encoding='utf8')
        invoke(exe,home,'--project','object','--ci')
        cmake=home/'Project'/'object'/'generated_cmake'/'mylib'/'CMakeLists.txt'
        assert ' OBJECT' in cmake.read_text(encoding='utf8'),cmake.read_text()
        assert (home/'Project'/'object'/'generated_cmake'/'CMakePresets.json').exists()
        print('PASS: generated OBJECT library builds and presets survive CI regeneration')
        if os.name != 'nt' and shutil.which('objcopy'):
            toolbin=tmp/'RISC-V wrappers'; toolbin.mkdir()
            for tool in ('gcc','g++','objcopy'):
                path=toolbin/('riscv64-none-elf-'+tool)
                path.write_text('#!/bin/sh\nexec "'+shutil.which(tool)+'" "$@"\n',encoding='utf8')
                path.chmod(0o755)
            firmware=tmp/'firmware'; firmware.mkdir()
            (firmware/'main.cpp').write_text('int main(){return 0;}\n',encoding='utf8')
            invoke(exe,home,'--create','firmware','--source-dir',str(firmware))
            (home/'Project'/'firmware'/'config.cfg').write_text(
                'compiler=g++\nsource_files=main.cpp\noutput_file=out/demo.elf\n'+
                'toolchain_prefix='+str(toolbin)+'/riscv64-none-elf-\n',encoding='utf8')
            invoke(exe,home,'--project','firmware','--ci')
            assert all((firmware/'out'/('demo'+ext)).is_file()
                       for ext in ('.elf','.bin','.hex'))
            print('PASS: prefixed toolchain resolution and ELF to BIN/HEX conversion')
            # Simulate a user-owned binary with a colliding name.
            (firmware/'out'/'demo.bin.pgr_make_owner').unlink()
            (firmware/'out'/'demo.bin').write_bytes(b'keep-user-data')
            invoke(exe,home,'--project','firmware','--ci',success=False)
            assert (firmware/'out'/'demo.bin').read_bytes()==b'keep-user-data'
            print('PASS: objcopy refuses to overwrite an unowned BIN file')
        # Script emulates MSVC on non-Windows; validates direct CLI arguments, not real MSVC ABI.
        if os.name!='nt':
            fakecl=tmp/'cl'; result=tmp/'captured-cl.txt'
            fakecl.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "'+str(result)+'"\n',encoding='utf8')
            fakecl.chmod(0o755)
            cfg.write_text(f'compiler={fakecl}\nsource_files=main.cpp\n'
                           'standard=-std=c++17\noptimization=-O2\n',encoding='utf8')
            invoke(exe,home,'--project','app',inp='17\ny\n\n0\n')
            args=result.read_text().splitlines()
            assert '/nologo' in args and '/O2' in args and '/std:c++17' in args and '/Tpmain.cpp' in args,args
            assert not any(a.startswith('-') for a in args),args
            print('PASS: MSVC-specific frontend invocation with fake cl executable')
        cfg.write_text('compiler=g++\nsource_files=main.cpp\n',encoding='utf8')
        (src/'main.cpp').write_text('int main(){return 0;}\n',encoding='utf8')
        invoke(exe,home,'--project','app','--ci')
        if shutil.which('clangd'):
            output=invoke(exe,home,'--project','app','--analyze')
            diag=json.loads(invoke(exe,home,'--project','app','--diagnostics'))
            assert isinstance(diag['diagnostics'],list)
            print('PASS: clangd --check executes against compile_commands.json')
            (src/'main.cpp').write_text('int main(){return undeclared_name;}\n',encoding='utf8')
            invoke(exe,home,'--project','app','--analyze',success=False)
            diag=json.loads(invoke(exe,home,'--project','app','--diagnostics'))
            assert any(x['tool']=='clangd' and x['line']==1 and
                       x['file'].endswith('main.cpp') and x['code']
                       for x in diag['diagnostics']),diag
            print('PASS: clangd diagnostic codes and source line are parsed')
            # A reopened TUI must show persisted diagnostics, not just session-local ones.
            ui=invoke(exe,home,'--project','app',inp='0\n')
            assert 'Диагностика' in ui and '1 записей' in ui, ui[-1800:]
            print('PASS: TUI restores last structured diagnostics across restarts')
        else:
            print('SKIP: clangd not installed')


if __name__=='__main__':
    main(Path(sys.argv[1]).resolve())
