Put extracted toolchain files here. PGR_Make.exe searches these directories
and prepends their bin folders to its own PATH:

  C:\PGR\Pack\GCC\bin\g++.exe
  C:\PGR\Pack\GCC\bin\gcc.exe
  C:\PGR\Pack\LLVM\bin\clang++.exe
  C:\PGR\Pack\CMake\bin\cmake.exe
  C:\PGR\Pack\Ninja\ninja.exe

Other supported layouts include Pack\bin, Pack\<tool>\bin,
Pack\<tool>\ucrt64\bin, Pack\<tool>\mingw64\bin, and
Pack\<tool>\<subfolder>\bin.

Do not copy only g++.exe: the compiler often needs libraries and auxiliary
programs. Extract the full working toolchain including its runtime DLLs.
PGR_Make.exe's static linking does NOT make GCC/CMake/Clang static.
Pack is intentionally empty in the source distribution.
