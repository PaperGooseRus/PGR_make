@echo off
setlocal

REM --- UTF-8 в консоли ---
chcp 65001 >nul

REM --- Чистая пересборка ---
if exist build rmdir /s /q build
mkdir build

REM --- Компиляция ---
REM   -std=c++17             — нужен для std::filesystem.
REM   -finput-charset=UTF-8  — явно указать кодировку исходников.
REM   -fno-declone-ctor-dtor — обход бага GCC 16 (GCC Bug 125359).
REM   БЕЗ -fexec-charset: литералы остаются в UTF-8, chcp 65001 их поймёт.

g++ main.cpp -std=c++17 -Os -finput-charset=UTF-8 -fno-declone-ctor-dtor -o build\main.exe
if errorlevel 1 (
    echo.
    echo Build failed.
    pause
    exit /b 1
)

cls
.\build\main.exe %*
endlocal