#!/usr/bin/env python3
"""Conservative zip extractor used by PackManager on POSIX systems.

Reject links, absolute and traversal paths, duplicate entries, oversized files,
ZIP bombs, and paths that would escape the staging directory.
"""
import os
import pathlib
import stat
import sys
import zipfile

MAX_TOTAL = 6 * 1024**3
MAX_FILES = 50000
MAX_ENTRY = 1024**3

def extract(archive, target):
    root = pathlib.Path(target).resolve(strict=True)
    names = set()
    total = 0
    with zipfile.ZipFile(archive) as z:
        infos = z.infolist()
        if len(infos) > MAX_FILES:
            raise ValueError('too many archive entries')
        for info in infos:
            name = info.filename.replace('\\', '/')
            parts = pathlib.PurePosixPath(name).parts
            if (not name or name.startswith('/') or ':' in name or
                any(p in ('.', '..') for p in parts) or
                name.startswith('//') or '\x00' in name):
                raise ValueError(f'unsafe archive entry: {name!r}')
            canonical = '/'.join(parts).rstrip('/').lower()
            if canonical in names:
                raise ValueError('duplicate archive entry')
            names.add(canonical)
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValueError('archive symlink forbidden')
            if info.file_size > MAX_ENTRY:
                raise ValueError('archive entry too large')
            total += info.file_size
            if total > MAX_TOTAL:
                raise ValueError('archive too large')
            dest = root.joinpath(*parts)
            if not dest.resolve(strict=False).is_relative_to(root):
                raise ValueError('archive path escapes target')
        for info in infos:
            dest = root.joinpath(*pathlib.PurePosixPath(info.filename.replace('\\','/')).parts)
            if info.is_dir():
                dest.mkdir(parents=True, exist_ok=True)
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, open(dest, 'xb') as dst:
                copied = 0
                while True:
                    buf = src.read(65536)
                    if not buf:
                        break
                    copied += len(buf)
                    if copied > MAX_ENTRY:
                        raise ValueError('archive entry exceeds quota')
                    dst.write(buf)

if __name__ == '__main__':
    try:
        extract(sys.argv[1], sys.argv[2])
    except Exception as exc:
        print(f'Unsafe/invalid ZIP: {exc}', file=sys.stderr)
        sys.exit(1)
