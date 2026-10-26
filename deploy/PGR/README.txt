PGR_Make portable layout:

C:\PGR\PGR_Make.exe   - statically linked main program (build on Windows)
C:\PGR\Pack\         - external tools (GCC, Clang, CMake, Ninja, ARM GCC...)
C:\PGR\Project\      - registered project configurations, not sources

The program resolves its folders relative to the executable directory.
Source code can live elsewhere (D:\Work\Project, etc.).

Start PGR_Make.exe; select an existing project or create one by entering its
name and source directory. You can then configure/build it in the TUI.

CI example:
  C:\PGR\PGR_Make.exe --project Calculator --ci --clean

There are NO compilers in this archive. Install them separately to Pack.

New build layouts:
  <SOURCE>\.pgr_build\<project-name>\  - PGR-owned CMake artifacts
  <INSTALL>\Project\<project-name>\  - config, logs, generated CMake
Use TUI menu 31 to select a specific compiler from Pack, 32 for an existing
CMakeLists.txt; CI supports --native-cmake and --toolchains.
Never remove PGR ownership markers manually from active build directories.
