@echo off
setlocal

chcp 65001 >nul

cls

if exist build rmdir /s /q build
rem if exist — чтобы не выводить ошибку, если каталога ещё нет
rem rmdir — удаление каталога в Windows
rem /s — рекурсивно (со всем содержимым)
rem /q — без вопросов подтверждения

mkdir build

g++ main.cpp -std=c++17 -Os -fno-declone-ctor-dtor -o build\main.exe
if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

cls
.\build\main.exe %*
endlocal