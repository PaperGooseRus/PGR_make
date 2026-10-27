#!/usr/bin/env python3
"""Opt-in fixture runner for user-supplied architectures and target kinds.

Runs only explicitly enabled entries. Copies source trees before building, and
never modifies the source fixtures. Do not use on untrusted source archives:
building a project executes its build scripts/toolchain.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

parser=argparse.ArgumentParser()
parser.add_argument('manifest',type=Path)
parser.add_argument('--exe',type=Path,required=True)
options=parser.parse_args()
manifest=options.manifest.resolve()
cli=options.exe.resolve()
assert cli.is_file(), 'PGR_Make executable missing'
data=json.loads(manifest.read_text(encoding='utf-8'))
assert data.get('schema')==1 and isinstance(data.get('fixtures'),list),'Invalid fixture manifest'
seen=set();count=0
for entry in data['fixtures']:
    name=entry['name']
    assert isinstance(name,str) and name and name not in ('.','..') and name not in seen and '/' not in name and '\\' not in name
    seen.add(name)
    if not entry.get('enabled',False):
        print('SKIP',name,'(disabled)')
        continue
    source=(manifest.parent/entry['source']).resolve()
    assert source.is_dir(),f'{name}: missing source {source}'
    mode=entry.get('mode','native')
    assert mode in ('native','generated','direct'),f'{name}: invalid mode'
    with tempfile.TemporaryDirectory(prefix='pgr_arch_'+name+'_') as tmp:
        root=Path(tmp);home=root/'PGR';home.mkdir()
        cloned=root/'sources'/name
        # Explicitly refuse fixtures with filesystem links to other locations.
        if any(p.is_symlink() for p in source.rglob('*')):
            raise RuntimeError(f'{name}: symlinks in source tree are not supported by this runner')
        shutil.copytree(source,cloned,ignore=shutil.ignore_patterns(
            '.git','.pgr_build','.pgr_objects_*','build','build-*','build_*','out','__pycache__'))
        def run(*args):
            proc=subprocess.run([str(cli),'--home',str(home),*map(str,args)],
                                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                text=True,errors='replace',timeout=entry.get('timeout_seconds',180))
            return proc
        result=run('--create',name,'--source-dir',cloned)
        if result.returncode:
            raise RuntimeError(f'{name}: registration failed\n{result.stdout[-3000:]}')
        if entry.get('config'):
            supplied=(manifest.parent/entry['config']).resolve()
            assert supplied.is_file(),f'{name}: config not found: {supplied}'
            shutil.copyfile(supplied,home/'Project'/name/'config.cfg')
        args=['--project',name,'--ci']
        if mode=='native':args.append('--native-cmake')
        elif mode=='direct':args.append('--direct')
        result=run(*args)
        expected=entry.get('expect','success')
        assert expected in ('success','failure')
        if (result.returncode==0)!=(expected=='success'):
            raise RuntimeError(f'{name}: expected {expected}, rc={result.returncode}\n{result.stdout[-6000:]}')
        for output in entry.get('artifacts',[]):
            assert '..' not in Path(output).parts and not Path(output).is_absolute(), f'{name}: invalid artifact path'
            assert (cloned/output).is_file(),f'{name}: expected artifact missing: {output}'
        count+=1
        print('PASS',name,'mode='+mode,'result='+expected)
if count == 0:
    print('No enabled fixtures; no architecture tests were executed',file=sys.stderr)
    sys.exit(2)
print('Completed',count,'enabled architecture fixtures')
