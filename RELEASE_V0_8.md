# PGR_Make 0.8.0 · Hardening & CI

**Status: development candidate, not a 1.0.0 stable release.**

## Quality changes

- Real GitHub Actions quality gate: GCC and Clang on Ubuntu 24.04, and Windows UCRT64 (GUI mandatory in CI).
- Nine additional C++ unit test programs: SHA-256, strict JSON, build path ownership, Pack catalog validation, build config roundtrip, and tool resolution.
- Integration fixtures: pure C, C++, native CMake + static library, known compile failure, and refusal to clean foreign data.
- Windows Pack installation acceptance fixture using a real Ninja executable (no downloaded test binaries).
- Every registered test is called through CTest; smoke/contract tests are distinguished from executable GUI tests.
- Local runner `python3 ci/verify.py`; CTest labeling makes module failures obvious.
- Tag-triggered CD produces **draft** GitHub Releases, never automatically published stable releases.
- Windows staging bundles GTK runtime and records SHA-256 of the ZIP; installation on a clean Windows device still needs human validation.

## Explicit remaining release gates

1. Run Windows GTK4 application manually on Windows 10/11 x64 at 100/125/150% DPI, including both Paned controls and project switch.
2. Verify bundled GTK resources, icons, themes, font rendering, and DLL resolution with MSYS2 *removed* from PATH.
3. Exercise CMake, Ninja and GCC installs on clean Windows, record SHA-256 and license notices. Pack download tests must be explicitly authorized.
4. Test upgrading a real `C:\PGR` from 0.7 with backups of Project and Pack.
5. Supply the project's actual license and third-party license notices before public distribution.
6. Run user-created ARM / RISC-V / embedded fixtures against the real hardware/toolchain, not simulated wrappers.

Do not label these tasks as complete based on a passing Linux CTest run.

## Release conditions

- No failing required CI jobs; no known high-severity corruption/data loss bug.
- Windows native GUI builds and the portable candidate is tested on a clean machine.
- Tested binaries match published checksums; GitHub release remains a draft until reviewed.
- Any unsupported architecture/toolchain is clearly marked experimental.
