@echo off
setlocal
chcp 65001 >nul
pushd "%~dp0"
REM Run inside MSYS2 UCRT64 with GCC, CMake and Ninja in PATH.
where g++ >nul 2>&1 || (echo ERROR: g++ not found. Use MSYS2 UCRT64. & popd & exit /b 1)
where cmake >nul 2>&1 || (echo ERROR: cmake not found. & popd & exit /b 1)
where ninja >nul 2>&1 || (echo ERROR: ninja not found. & popd & exit /b 1)
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DPGR_STATIC_WINDOWS=ON -DPGR_BUILD_GUI=ON -DPGR_REQUIRE_GUI=ON
if errorlevel 1 (popd & exit /b 1)
cmake --build build-win --parallel
if errorlevel 1 (popd & exit /b 1)
if not exist "build-win\PGR_Make.exe" (echo ERROR: CLI exe not produced. & popd & exit /b 1)
if not exist "build-win\PGR_Make_GUI.exe" (echo ERROR: GTK4 GUI exe not produced. & popd & exit /b 1)
if not exist "deploy\PGR\Pack" mkdir "deploy\PGR\Pack"
if not exist "deploy\PGR\Project" mkdir "deploy\PGR\Project"
if not exist "deploy\PGR\tools" mkdir "deploy\PGR\tools"
copy /Y "tools\zip_guard.ps1" "deploy\PGR\tools\zip_guard.ps1" >nul
copy /Y "tools\zip_guard.py" "deploy\PGR\tools\zip_guard.py" >nul
if not exist "deploy\PGR\Pack\catalog.json" copy "tools\catalog.json" "deploy\PGR\Pack\catalog.json" >nul
copy /Y "build-win\PGR_Make.exe" "deploy\PGR\PGR_Make.exe" >nul
if errorlevel 1 (popd & exit /b 1)
copy /Y "build-win\PGR_Make_GUI.exe" "deploy\PGR\PGR_Make_GUI.exe" >nul
if errorlevel 1 (popd & exit /b 1)
copy /Y "start_gui.bat" "deploy\PGR\start_gui.bat" >nul
if errorlevel 1 (popd & exit /b 1)
echo.
echo Done: deploy\PGR\PGR_Make.exe
echo Copy the CONTENTS of deploy\PGR to C:\PGR\
echo Ensure compilers and CMake are in Pack; GTK4 runtime DLLs in Pack\GTK4\bin.
echo Launch GUI with C:\PGR\start_gui.bat
echo GTK4 needs its DLLs/resources; the GUI is not a single-file static binary.
where objdump >nul 2>&1 && objdump -p "deploy\PGR\PGR_Make.exe" | findstr /C:"DLL Name"
popd
exit /b 0
