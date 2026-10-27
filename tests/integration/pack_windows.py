#!/usr/bin/env python3
"""Windows UCRT64 real-pack import smoke test; no network or shell scripts."""
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

exe=Path(sys.argv[1]).resolve()
tool=shutil.which('ninja')
assert tool, 'ninja.exe is needed for Windows Pack acceptance'
version=subprocess.check_output([tool,'--version'],text=True).strip().splitlines()[0]
assert re.fullmatch(r'[A-Za-z0-9._+-]+',version),version
id=f'ninja@{version}@windows-x86_64'
with tempfile.TemporaryDirectory(prefix='pgr_windows_pack_') as raw:
    base=Path(raw)
    home=base/'PGR';home.mkdir()
    (home/'tools').mkdir()
    shutil.copy2(Path(__file__).resolve().parents[2]/'tools'/'zip_guard.ps1',home/'tools')
    src=base/'source';src.mkdir()
    (src/'main.cpp').write_text('int main(){return 0;}\n',encoding='utf-8')
    def invoke(*args,ok=True):
        p=subprocess.run([str(exe),'--home',str(home),*map(str,args)],capture_output=True,text=True,errors='replace',timeout=90)
        assert (p.returncode==0)==ok,(args,p.returncode,p.stdout,p.stderr)
        return p.stdout+p.stderr
    invoke('--create','Example','--source-dir',src)
    archive=base/'ninja.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        z.write(tool,'ninja.exe')
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    invoke('--pack-import-custom',id,archive,'0'*64,'ninja.exe',ok=False)
    invoke('--pack-import-custom',id,archive,digest,'ninja.exe')
    assert id in invoke('--pack-list')
    invoke('--project','Example','--pack-bind','ninja',id)
    invoke('--pack-retire',id,ok=False)
    invoke('--project','Example','--pack-unbind','ninja')
    invoke('--pack-retire',id)
    assert id not in invoke('--pack-list')
print('PASS: Windows ZIP import/version/pin/retire')
