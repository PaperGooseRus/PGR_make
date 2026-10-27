#!/usr/bin/env python3
"""Shared BuildController keeps independent, append-only history per project."""
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="pgr history ") as folder:
    home = Path(folder) / "PGR"
    src = Path(folder) / "Source folder"
    src.mkdir()
    home.mkdir()
    (src / "main.cpp").write_text('int main(){return 0;}\n', encoding="utf8")
    def call(*args):
        result = subprocess.run([str(exe), "--home", str(home), *args],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
    call("--create", "Example", "--source-dir", str(src))
    call("--project", "Example", "--ci", "--direct")
    call("--project", "Example", "--ci")
    store = home / "Project" / "Example"
    lines = (store / "history.log").read_text(encoding="utf8").splitlines()
    assert len(lines) == 2, lines
    assert "direct" in lines[0] and "generated-cmake" in lines[1], lines
    assert len(list((store / "history").glob("*.json"))) >= 1
    assert not (src / "CMakeLists.txt").exists()
print("PASS: common controller build history without touching sources")
