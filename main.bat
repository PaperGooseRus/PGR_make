@echo off
setlocal
chcp 65001 >nul
REM Safe incremental build. Never delete the build directory automatically.
if not exist build mkdir build
if errorlevel 1 exit /b 1

g++ -std=c++17 -Os -Wall -Wextra -finput-charset=UTF-8 main.cpp -o build\PGR_make.exe
if errorlevel 1 (
    echo ERROR: C++ compilation failed.
    exit /b 1
)

build\PGR_make.exe %*
exit /b %errorlevel%
