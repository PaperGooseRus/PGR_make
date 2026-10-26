@echo off
setlocal

REM --- UTF-8 в консоли (русские строки в TUI) ---
chcp 65001 >nul

REM --- Чистая пересборка ---
if exist build rmdir /s /q build
mkdir build

REM --- Компиляция ---
REM   -std=c++17             — нужен для std::filesystem.
REM   -fexec-charset=CP866   — строковые литералы в CP866 (совместимо с cmd).
REM   -fno-declone-ctor-dtor — обход бага GCC 16 (GCC Bug 125359).

g++ main.cpp -std=c++17 -Os -fexec-charset=CP866 -fno-declone-ctor-dtor -o build\main.exe
if errorlevel 1 (
    echo.
    echo Build failed.
    pause
    exit /b 1
)

cls
.\build\main.exe %*
endlocal