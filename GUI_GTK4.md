> **GUI 0.2:** интерфейс на GtkPaned с полным набором команд TUI описан в [GUI_PANED.md](GUI_PANED.md).

# PGR_Make GTK4 GUI (0.1)

GTK4 GUI is a separate, optional frontend for the same PGR_Make CLI/backend. It does **not** rewrite `CMakeLists.txt` in the source tree, nor does it create a second build engine.

## Windows layout

```text
C:\PGR\
  PGR_Make.exe           (CLI/TUI; MinGW runtime may be statically linked)
  PGR_Make_GUI.exe       (GTK4 graphical frontend)
  start_gui.bat          (adds Pack/GTK4/bin to PATH before launch)
  Pack\
    GCC\bin\...         (optional compiler toolchains)
    LLVM\bin\...
    CMake\bin\...
    GTK4\bin\...        (GTK4 and dependency DLLs, **not** supplied in source ZIP)
    GTK4\share\...      (GTK4 resource files and GLib settings, if bundled)
  Project\
    <project>\
      project.pgr         (registered source location)
      config.cfg          (existing PGR configuration)
      gui.cfg             (GUI build mode only)
      diagnostics.json
      work.log
```

## Build in MSYS2 UCRT64

Install the development toolchain and GTK4 (the `pacman` packages are provided by MSYS2):

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk4
```

From an **MSYS2 UCRT64 terminal**, in the project source directory:

```bash
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPGR_BUILD_GUI=ON -DPGR_REQUIRE_GUI=ON -DPGR_STATIC_WINDOWS=ON
cmake --build build-win --parallel
```

Or run `build_windows.bat` in UCRT64. It copies **both** executables into `deploy/PGR/`, along with the launcher script. Then run `install_to_c_pgr.bat` to install without overwriting existing `Project` and `Pack` contents. To bundle runtime DLLs and resources from a local UCRT64 installation into `deploy/PGR/Pack/GTK4` before installation, optionally run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\bundle_gtk_runtime.ps1
```

This helper recursively copies MSYS2 UCRT64 DLL dependencies using `objdump` and common GTK runtime data directories. **It is not a substitute for testing on a clean machine** and may require additional resources for advanced GTK features. Check third-party licenses when redistributing.

The build deliberately fails with `PGR_REQUIRE_GUI=ON` if the GTK4 development package is unavailable. With `PGR_REQUIRE_GUI=OFF`, CLI builds still work even on machines without GTK4.

**Important:** The CLI's GCC C++ runtime may be statically linked. GTK4 itself and its dependencies are **normally dynamically linked** on Windows (the MSYS2 GTK4 package provides import libraries and DLLs). A standalone `PGR_Make_GUI.exe` is **not** a self-contained static binary. For a portable deployment, bundle and test the GTK4 runtime (DLLs, GSettings schemas, themes/data resources and loaders) under `Pack/GTK4`, or run from a machine with a compatible GTK4/MSYS2 installation. The source ZIP does not bundle proprietary/third-party binaries, and `start_gui.bat` does not install them automatically.

Run GUI via `C:\PGR\start_gui.bat` so the local GTK4 runtime is discoverable. Running the EXE directly requires GTK4 DLLs already on Windows' DLL search path.

## Linux

Install the `gtk4` development package with your distribution's package manager, then:

```bash
cmake -S . -B build -DPGR_BUILD_GUI=ON -DPGR_REQUIRE_GUI=ON
cmake --build build
./build/PGR_Make_GUI
```

Place `PGR_Make_GUI` and `PGR_Make` in the **same** directory. `Project` and `Pack` directories are created relative to the EXE.

## GUI features

- Registered project list, refresh and project creation with name, source directory and optional starter `main.cpp`.
- Compiler entry with explicit selection of detected executables under `Pack`.
- Debug/Release/custom optimization selection, stored in existing `config.cfg`.
- Build modes: `PGR CMake`, `Existing CMakeLists.txt`, and `Direct compiler`.
- Save settings, start build, show combined stdout/stderr without freezing GUI.
- Optional clangd analysis (after a CMake build), structured compiler/linker diagnostics, jump to source via `code --goto` (otherwise OS default editor).
- Existing CLI/TUI functionality and `--ci` preserved.

**Limitations of GUI 0.1:** No embedded code editor, no standalone package manager UI, no fine-grained editing of include/linker flags (the existing TUI and config files still handle those), no persistent LSP server. The Stop button forcibly terminates the **CLI process**; descendant compiler/CMake processes may require manual termination, depending on OS. Build logs remain available in `Project/<name>/work.log`. Direct compiler mode uses the project's configured `build/...` output and does not perform incremental builds.

### Testing

CLI functionality and GUI backend contracts (no GTK dependency):

```bash
python3 tests/run_tests.py build/PGR_Make
python3 tests/run_features.py build/PGR_Make
python3 tests/run_gui_bridge.py build/PGR_Make
ctest --test-dir build --output-on-failure
```

GUI compile/run smoke test (requires installed GTK4 and a display):

1. Build with `PGR_REQUIRE_GUI=ON`, verify `PGR_Make_GUI` is produced.
2. Register the existing `PGR_Window_Test` project.
3. In GUI select project and existing CMake mode; Build; inspect logs and diagnostics.
4. Add a syntax error; rebuild and double-click its diagnostic row.
5. Close/reopen GUI to verify project selection and `gui.cfg` build mode.
6. On Windows verify launcher with the bundled GTK4 DLL/resource tree on a clean PC.

No claim of a native Windows GUI build is made by the Linux-only tests.
