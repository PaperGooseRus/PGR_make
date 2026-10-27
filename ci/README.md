# CI/CD & Testing Guide

## Local verification

Run from the project directory:

```bash
python3 ci/verify.py
python3 ci/verify.py --label unit
# With GTK4 development files installed:
python3 ci/verify.py --require-gui
```

The runner configures a separate `out/local-ci` build directory; it does not mutate any registered project. The CTest integration fixtures create and destroy their *own* temporary projects.

For a manual Windows release check use the same CMake/Ninja commands in `.github/workflows/quality.yml` from MSYS2 UCRT64. If GTK4 or Python are absent with their required toggles set, configuration fails, rather than silently skipping GUI or integration tests.

## Test coverage map

| Module / layer | Tests | Kind |
|---|---|---|
| SHA-256 | `unit_hash` | executable unit |
| JSON reader | `unit_json` | executable unit |
| Safe build directories | `unit_paths`, `run_tests` | executable unit, integration |
| Pack schema and version handling | `unit_pack_schema`, `pack_manager_regression`, `pack_windows_acceptance` | unit, Linux integration, Windows integration |
| Tool resolution / compiler families | `unit_tool_resolution`, `unit_toolchain`, `run_features` | unit, integration |
| Build config and flags | `unit_config`, `run_tests`, `run_features` | unit, integration |
| Package manager/CMake Presets | `unit_presets`, `run_features` | unit, integration |
| Project setup | `unit_project_setup`, `workspace_management` | unit, integration |
| Build controller / CMake backends | `cmake_project_root`, `run_tests`, `acceptance_matrix` | unit, integration |
| Workspace/project registry | `workspace_management`, `run_gui_bridge` | unit, integration |
| Process execution/cancellation | `process_cancellation`, `run_tests` | unit, integration |
| Compiler diagnostics | `diagnostics_parsing`, `run_features`, `acceptance_matrix` | unit, integration |
| GUI preferences and layout | `gui_ux_model`, `gui_layout_preferences`, `gui_v07_contract` | unit, source contract |
| GTK4 actual window | mandatory build on Linux + Windows CI | compilation, **not** interactive GUI test |
| External targets, ARM/RISC-V | optional `ci/architecture_matrix.py` | opt-in acceptance, not included in normal CI |

`contract` tests only inspect source/API invariants. They **must not** be presented as proof that GTK4 renders correctly.

## GitHub Actions

On every push/PR the quality workflow builds CLI and GTK4 on Linux GCC/Clang, runs full CTest, and checks Windows UCRT64 GUI plus the complete Windows-compatible CTest suite (the POSIX-only Pack fixture is replaced with a native Windows Pack test). The Windows candidate is uploaded as a CI artifact, not installed automatically.

On a `v*` git tag the final job runs only if both Linux and Windows passed; it downloads the Windows candidate and creates a GitHub **draft** release with a SHA-256 checksum. Review licensing, runtime DLLs, Windows GUI appearance and smoke tests before manually publishing it.

**Repository setup:** place `.github/workflows/quality.yml` in the root of the GitHub repository and push it. Enable Actions, ensure workflow permissions permit draft release creation (or use appropriate repository policy), protect `main` by requiring Linux and Windows jobs. The assistant has prepared the workflow, but has not connected, pushed or executed it on GitHub.

## Security and process limits

Build/test commands are non-interactive and use temporary project homes. Tool installation from remote URLs is never invoked by the standard matrix. Archive extraction is guarded by paths/checksums in Pack Manager. Signing installers, trusted package catalogs, reproducible builds across MSYS2 updates, and full dependency licensing are future quality gates.

## Future embedded and architecture fixtures

When test projects are ready, copy `ci/architecture_matrix.example.json` to a local manifest outside the repository and point it at real test projects. Mark an entry `enabled: true` only after its toolchain has been installed. Run:

```bash
python3 ci/architecture_matrix.py /path/to/manifest.json --exe out/local-ci/PGR_Make
```

The runner creates a private PGR workspace and copies source fixtures into a temporary directory; it never builds in the original source tree. Optional `config` supplies an existing PGR config file. Check actual outputs via `artifacts` relative to the cloned project. For other architectures, you must still verify the output binary's architecture and execution/flash behavior on the actual target; exit code zero alone does not prove compatibility. Fixtures containing filesystem symlinks are refused.

Do not put credentials, destructive post-build commands, or unreviewed CMake code into fixtures. This runner intentionally executes the projects' build scripts.
