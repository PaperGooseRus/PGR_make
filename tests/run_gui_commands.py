#!/usr/bin/env python3
"""New GUI-to-CLI operations; runs without GTK or a display server."""
import subprocess
import tempfile
import sys
from pathlib import Path

cli = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="PGR GUI commands ") as tmp:
    root = Path(tmp)
    home = root / "PGR"
    home.mkdir()
    source = root / "source with spaces"
    source.mkdir()
    (source / "main.cpp").write_text("int main(){return 0;}\n", encoding="utf8")

    def call(*args, rc=0):
        proc = subprocess.run([str(cli), "--home", str(home), *args],
                              capture_output=True, text=True, timeout=65)
        assert proc.returncode == rc, (args, proc.stdout, proc.stderr)
        return proc.stdout

    call("--create", "GuiTools", "--source-dir", str(source))
    project = home / "Project" / "GuiTools"
    cfg = project / "config.cfg"
    with cfg.open("a", encoding="utf8") as fd:
        fd.write("package_dirs=vendor/cmake\n")
    command = call("--project", "GuiTools", "--show-command")
    assert "g++" in command and "main.cpp" in command, command
    assert "Generated" in call("--project", "GuiTools", "--generate-only")
    assert (project / "generated_cmake" / "CMakeLists.txt").is_file()
    call("--project", "GuiTools", "--ci")
    assert (source / ".pgr_build" / "GuiTools" / "CMakeCache.txt").exists()
    call("--project", "GuiTools", "--export-commands")
    assert (source / "compile_commands.json").is_file()
    # Never overwrite a user-provided compilation database.
    (source / "compile_commands.json").write_text("USER DATA", encoding="utf8")
    assert subprocess.run([str(cli), "--home", str(home), "--project", "GuiTools",
                           "--export-commands"], capture_output=True).returncode != 0
    assert (source / "compile_commands.json").read_text(encoding="utf8") == "USER DATA"
    assert "Cleaned" in call("--project", "GuiTools", "--clean-only")
    assert not (source / ".pgr_build" / "GuiTools").exists()
    # The user-owned directory is never cleaned merely because it matches a naming pattern.
    foreign = source / "build_user"
    foreign.mkdir()
    marker = foreign / "valuable.txt"
    marker.write_text("DO NOT ERASE", encoding="utf8")
    assert subprocess.run([str(cli), "--home", str(home), "--project", "GuiTools",
                           "--clean-only", "--build-dir", "build_user"],
                          capture_output=True).returncode != 0
    assert marker.read_text(encoding="utf8") == "DO NOT ERASE"
    # A locally selected CMake package directory must reach native CMake builds.
    pkg = source / "vendor" / "cmake" / "Foo"
    pkg.mkdir(parents=True)
    (pkg / "FooConfig.cmake").write_text("add_library(Foo::Foo INTERFACE IMPORTED)\n", encoding="utf8")
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.16)\nproject(External LANGUAGES CXX)\n"
        "find_package(Foo REQUIRED)\nadd_executable(testfoo main.cpp)\n"
        "target_link_libraries(testfoo PRIVATE Foo::Foo)\n", encoding="utf8")
    call("--project", "GuiTools", "--ci", "--native-cmake")
    assert (source / ".pgr_build" / "GuiTools" / "CMakeCache.txt").exists()
    assert "package_dirs=vendor/cmake" in cfg.read_text(encoding="utf8")

print("PASS: GUI commands, prefix paths, safe cleaning, native CMake")
