#!/usr/bin/env python3
"""End-to-end Pack Manager tests. No network or Windows SDK required."""
import hashlib
import re
import shutil
import io
import os
import pathlib
import platform
import subprocess
import sys
import tempfile
import zipfile

exe = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(__file__).resolve().parents[1]
checks = 0

def run(home, *args, ok=True):
    global checks
    cmd = [str(exe), '--home', str(home), *map(str, args)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    assert (result.returncode == 0) == ok, f'{cmd}: {result.returncode}\n{result.stdout}\n{result.stderr}'
    checks += 1
    return result

with tempfile.TemporaryDirectory(prefix='pgr pack manager ') as tmp:
    home = pathlib.Path(tmp)/'app'
    (home/'tools').mkdir(parents=True)
    (home/'tools'/'zip_guard.py').write_bytes((source/'tools'/'zip_guard.py').read_bytes())
    (home/'Pack').mkdir()
    (home/'Pack'/'catalog.json').write_text('{"schema":1,"packages":[]}', encoding='utf-8')
    project = pathlib.Path(tmp)/'project code';project.mkdir()
    (project/'src').mkdir()
    (project/'src'/'main.cpp').write_text('int main(){return 0;}')
    run(home,'--create','Example','--source-dir',project)

    version='9.7.2'
    id=f'ninja@{version}@linux'
    archive=pathlib.Path(tmp)/'good.zip'
    with zipfile.ZipFile(archive,'w') as z:
        z.writestr('ninja', f'#!/bin/sh\n[ "$1" = "--version" ] && echo {version} && exit 0\nexit 0\n')
    sha=hashlib.sha256(archive.read_bytes()).hexdigest()
    assert run(home,'--pack-import-custom',id,archive,'0'*64,'ninja',ok=False).returncode != 0
    assert not (home/'Pack'/'ninja').exists()
    run(home,'--pack-import-custom',id,archive,sha,'ninja')
    listed=run(home,'--pack-list').stdout
    assert id in listed and 'ready' in listed
    executable=home/'Pack'/'ninja'/'9.7.2-linux'/'ninja'
    original=executable.read_bytes()
    executable.write_bytes(original+b'\n# changed contents\n')
    assert 'invalid' in run(home,'--pack-list').stdout
    run(home,'--project','Example','--pack-bind','ninja',id,ok=False)
    executable.write_bytes(original)
    run(home,'--project','Example','--pack-bind','ninja',id)
    config=(home/'Project'/'Example'/'config.cfg').read_text()
    assert 'ninja_id='+id in config
    run(home,'--pack-retire',id,ok=False)
    run(home,'--project','Example','--pack-unbind','ninja')
    run(home,'--pack-retire',id)
    assert (home/'Pack'/'Retired'/id/'ninja').is_file()
    assert id not in run(home,'--pack-list').stdout

    # Block archive traversal even with a correctly supplied digest.
    evil=pathlib.Path(tmp)/'evil.zip'
    with zipfile.ZipFile(evil,'w') as z:
        z.writestr('../out-of-tree.txt','do not write')
        z.writestr('ninja',f'#!/bin/sh\necho {version}\n')
    bad_id='ninja@9.7.3@linux'
    run(home,'--pack-import-custom',bad_id,evil,hashlib.sha256(evil.read_bytes()).hexdigest(),'ninja',ok=False)
    assert not (home/'Pack'/'ninja'/'9.7.3-linux').exists()
    assert not (home/'Pack'/'out-of-tree.txt').exists()

    # Real system tools registered as external versions; check cache isolation.
    cmake = shutil.which('cmake')
    ninja = shutil.which('ninja')
    if cmake and ninja:
        cmake_version = re.search(r'\d+\.\d+(?:\.\d+)?', subprocess.check_output([cmake,'--version'],text=True)).group(0)
        ninja_version = subprocess.check_output([ninja,'--version'],text=True).strip().splitlines()[0]
        for tool,path,version in [('cmake',cmake,cmake_version),('ninja',ninja,ninja_version)]:
            tool_id=f'{tool}@{version}@linux'
            run(home,'--pack-register',tool_id,pathlib.Path(path).parent,pathlib.Path(path).name)
            run(home,'--project','Example','--pack-bind',tool,tool_id)
        run(home,'--project','Example','--ci')
        nested=project/'.pgr_build'/'Example'/'environments'
        assert nested.is_dir() and any(nested.rglob('CMakeCache.txt')), 'pinned versions must get isolated cache'
        run(home,'--project','Example','--export-commands')
        assert (project/'compile_commands.json').is_file()
        run(home,'--project','Example','--clean-only')
        assert not any(nested.rglob('CMakeCache.txt'))
        assert (project/'src'/'main.cpp').is_file()
        run(home,'--project','Example','--pack-unbind','cmake')
        run(home,'--project','Example','--pack-unbind','ninja')
    # A pinned but missing version must fail rather than silently use PATH.
    cfg=home/'Project'/'Example'/'config.cfg'
    with cfg.open('a') as f:f.write('\ncmake_id=cmake@nonexistent@linux\n')
    failed=run(home,'--project','Example','--ci',ok=False)
    assert 'pinned tool' in failed.stderr or 'pinned tool' in failed.stdout

    # Strict checksum can detect changed archive contents.
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==sha

print(f'Pack Manager: {checks} CLI scenarios passed')
