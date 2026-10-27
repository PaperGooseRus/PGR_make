#!/usr/bin/env python3
"""CLI contract exercised by the GTK4 frontend; requires no GUI/display."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

cli = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="PGR GTK bridge ") as work:
    home = Path(work) / "portable"
    source = Path(work) / "source with spaces"
    home.mkdir()
    source.mkdir()
    (source / "main.cpp").write_text('int main(){return 0;}\n', encoding="utf-8")
    def call(*args, rc=0):
        result = subprocess.run([str(cli), "--home", str(home), *args],
                                capture_output=True, text=True, timeout=50)
        assert result.returncode == rc, f"{args}: {result.returncode}\n{result.stdout}\n{result.stderr}"
        return result
    call("--create", "GTK Demo", "--source-dir", str(source))
    assert "GTK Demo" in call("--list").stdout
    call("--project", "GTK Demo", "--ci", "--direct")
    assert (source / "build" / "GTK Demo").exists() or (source / "build" / "GTK Demo.exe").exists()
    report = json.loads((home / "Project" / "GTK Demo" / "diagnostics.json").read_text())
    assert report["diagnostics"] == [], report
    call("--project", "GTK Demo", "--ci")
    assert (source / ".pgr_build" / "GTK Demo" / "CMakeCache.txt").exists()
    assert (home / "Project" / "GTK Demo" / "config.cfg").is_file()
    # GUI mode is a view setting, not a write into CMakeLists.txt.
    assert not (source / "CMakeLists.txt").exists()
    # Invalid CLI combinations must be refused before starting a build.
    assert subprocess.run([str(cli), "--home", str(home), "--direct", "--project", "GTK Demo"],
                          capture_output=True).returncode != 0
print("PASS: GUI/CLI project creation, direct build, generated CMake build, diagnostics, isolation")
