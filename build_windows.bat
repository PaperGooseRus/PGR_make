@echo off
setlocal
chcp 65001 >nul
pushd "%~dp0"
REM Run inside MSYS2 UCRT64 with GCC, CMake and Ninja in PATH.
where g++ >nul 2>&1 || (echo ERROR: g++ not found. Use MSYS2 UCRT64. & popd & exit /b 1)
where cmake >nul 2>&1 || (echo ERROR: cmake not found. & popd & exit /b 1)
where ninja >nul 2>&1 || (echo ERROR: ninja not found. & popd & exit /b 1)
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DPGR_STATIC_WINDOWS=ON
if errorlevel 1 (popd & exit /b 1)
cmake --build build-win --parallel
if errorlevel 1 (popd & exit /b 1)
if not exist "build-win\PGR_Make.exe" (echo ERROR: Windows exe not produced. & popd & exit /b 1)
if not exist "deploy\PGR\Pack" mkdir "deploy\PGR\Pack"
if not exist "deploy\PGR\Project" mkdir "deploy\PGR\Project"
copy /Y "build-win\PGR_Make.exe" "deploy\PGR\PGR_Make.exe" >nul
if errorlevel 1 (popd & exit /b 1)
echo.
echo Done: deploy\PGR\PGR_Make.exe
echo Copy the CONTENTS of deploy\PGR to C:\PGR\
echo Ensure required compiler and CMake binaries are installed in C:\PGR\Pack\
where objdump >nul 2>&1 && objdump -p "deploy\PGR\PGR_Make.exe" | findstr /C:"DLL Name"
popd
exit /b 0
