# Windows UCRT64 CI hotfix for PGR_Make 0.8.0

Investigated failed GitHub Actions run: `PaperGooseRus/PGR_make`, run `40460118612`, Windows job `122042447321`.

## Fixes

1. `unit_paths`, `run_gui_commands`: close the PGR build ownership marker *before* removing its containing directory (`core/safe_paths.hpp`). Windows does not allow the deletion of a file that the process itself still has open. The unit test now captures the error text **after** the function call.
2. `pack_windows_acceptance`: load `System.IO.Compression.FileSystem` for Windows PowerShell 5.1 in `tools/zip_guard.ps1` (the `ZipFile` API is located there). Also capture and report the extractor stderr in `core/pack_manager.hpp` instead of emitting only a generic failure. ZIP validation, SHA-256 verification, staging, and rollback remain in place.
3. `run_tests`: before testing `-fsanitize=address`, compile and **link** a tiny program with the selected GCC. GCC 16.2 on this runner accepts the flag but the linker reports `cannot find -lasan`. All ordinary projects still run. When ASan cannot be linked, the suite explicitly prints `SKIP` for the unsupported optional case instead of claiming it passed.
4. `gui_ux_model`: accept LF/CRLF text history, with an explicit CRLF regression case. If a Windows mismatch persists, the test now prints the actual parsed entries rather than terminating with opaque `0xc0000409`.

## Checks performed

- Clean CMake Release builds on Linux GCC and Clang, CLI and C++ unit binaries.
- CTest: 26/26 on each of those two Linux toolchains.
- Actual Windows UCRT64 and GTK4 tests **still require a new GitHub Actions run** after committing this source update; no Windows-green result is claimed here.

## To validate Windows CI

Commit the changes and run `.github/workflows/quality.yml` again. The Windows job compiles both EXEs and executes `ctest --output-on-failure` including a native Ninja ZIP import.

To focus failures after checkout:

```sh
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=Release -DPGR_STATIC_WINDOWS=ON -DPGR_BUILD_GUI=ON -DPGR_REQUIRE_GUI=ON -DPGR_REQUIRE_PYTHON_TESTS=ON -DBUILD_TESTING=ON
cmake --build build-ci --parallel 2
ctest --test-dir build-ci --output-on-failure -R 'gui_ux_model|unit_paths|run_tests|run_gui_commands|pack_windows_acceptance'
```

The GitHub Actions `windows-test-logs` artifact and CTest output will provide detailed diagnostics if a Windows-specific regression remains.
