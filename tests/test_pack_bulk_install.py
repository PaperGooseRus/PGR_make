#!/usr/bin/env python3
"""Offline end-to-end import of a single archive providing two compiler tools."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile

exe=Path(sys.argv[1]).resolve()
source=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pgr_pack_bundle_') as temp:
    home=Path(temp)/'PGR'; (home/'tools').mkdir(parents=True)
    shutil.copy2(source/'tools'/'zip_guard.py',home/'tools')
    zip_path=Path(temp)/'bundle.zip'
    names={'toolchain/bin/g++': 'gcc (Test Suite) 16.2.0',
           'toolchain/bin/gcc': 'gcc (Test Suite) 16.2.0',
           'toolchain/bin/clang++': 'clang version 23.1.3',
           'toolchain/bin/clang': 'clang version 23.1.3'}
    with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,version in names.items():
            archive.writestr(name, '#!/bin/sh\necho "'+version+'"\n')
    package={'id':'gcc@16.2.0@linux','tool':'gcc','version':'16.2.0','platform':'linux',
             'url':'https://example.test/compiler.zip','sha256':hashlib.sha256(zip_path.read_bytes()).hexdigest(),
             'executable':'toolchain/bin/g++','provides':[{'tool':'clang','version':'23.1.3','executable':'toolchain/bin/clang++'}]}
    (home/'tools'/'catalog.json').write_text(json.dumps({'packages':[package]}),encoding='utf-8')
    def call(*args,expected=0):
        p=subprocess.run([str(exe),'--home',str(home),*args],capture_output=True,text=True,timeout=40)
        assert p.returncode==expected,(args,p.returncode,p.stdout,p.stderr)
        return p.stdout+p.stderr
    call('--pack-import',package['id'],str(zip_path))
    listed=call('--pack-list')
    assert 'gcc@16.2.0@linux\tready' in listed,listed
    assert 'clang@23.1.3@linux\tready' in listed,listed
    # A shared compiler archive must retire atomically; no dangling Clang entry.
    call('--pack-retire','gcc@16.2.0@linux')
    assert not call('--pack-list').strip(),'bundle metadata remained after retirement'
    assert (home/'Pack'/'Retired'/'gcc@16.2.0@linux').is_dir()
    # Bulk installation without all required categories must fail before networking.
    text=call('--pack-install-all',expected=1)
    assert 'missing catalog entry' in text,text
print('PASS: offline compiler bundle import, registry, shared retirement and bulk preflight')
