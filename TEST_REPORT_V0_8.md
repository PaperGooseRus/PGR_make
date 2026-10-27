# Test report · PGR_Make 0.8.0 (2026-10-27)

## Executed in this environment

| Check | Environment | Result |
|---|---|---|
| Configure/build CLI and unit test binaries | Linux, GCC, C++17, Ninja, Release | PASS |
| CTest complete suite (including acceptance) | Linux GCC | **26/26 PASS** |
| Configure/build CLI and unit test binaries | Linux, Clang, C++17, Ninja, Release | PASS |
| CTest complete suite (including acceptance) | Linux Clang | **26/26 PASS** |
| Optional fixture runner | Local CMake executable example | PASS |
| GitHub Actions YAML | Parsed, Linux/Windows/release jobs present | PASS (syntax only) |

## Not executed or not proven

- GTK4 compilation/running on Windows or Linux: GTK4 development SDK is not present in the test container. Build is **mandatory** in CI but the hosted workflow has not yet run on GitHub.
- Windows UCRT64 CTest and PowerShell ZIP import were not run locally.
- Online Pack downloads, executable GTK4 bundle on a clean Windows installation, source upgrades from 0.7 to 0.8, and licensed public distribution not verified.
- ARM/RISC-V/embedded toolchain behavior awaits user-created fixtures and hardware/toolchains.
- Optional ASan/UBSan configure/build was attempted but exceeded the environment time limit during compilation; no sanitizer results are claimed.

## Registered suite composition (Linux)

- 15 C++ unit-level tests (legacy and new).
- 7 integration/acceptance tests, including real GCC/CMake runs.
- 4 source-level GUI contracts. These are **not equivalent** to runtime GUI tests.

See [CI documentation](ci/README.md) for each module's corresponding tests and [release gates](RELEASE_V0_8.md).
