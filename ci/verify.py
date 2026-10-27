#!/usr/bin/env python3
"""Run the same CMake/CTest quality gate locally, without a CI service."""
import argparse
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', default='out/local-ci')
parser.add_argument('--require-gui', action='store_true', help='Fail if GTK4 is absent')
parser.add_argument('--asan', action='store_true', help='ASan+UBSan with GCC/Clang')
parser.add_argument('--label', help='CTest regex label, e.g. unit or integration')
options = parser.parse_args()
build = (root / options.build_dir).resolve()
args = ['cmake','-S',str(root),'-B',str(build),'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Debug' if options.asan else '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_TESTING=ON','-DPGR_REQUIRE_PYTHON_TESTS=ON',
        '-DPGR_BUILD_GUI=ON',
        '-DPGR_REQUIRE_GUI=' + ('ON' if options.require_gui else 'OFF'),
        '-DPGR_ENABLE_SANITIZERS=' + ('ON' if options.asan else 'OFF')]
for cmd in (args, ['cmake','--build',str(build),'--parallel','2'],
            ['ctest','--test-dir',str(build),'--output-on-failure','--timeout','180'] +
            (['-L',options.label] if options.label else [])):
    print('+', ' '.join(cmd), flush=True)
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
