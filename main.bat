@echo off
setlocal

cls

if exist build rmdir /s /q build
rem if exist — чтобы не выводить ошибку, если каталога ещё нет
rem rmdir — удаление каталога в Windows
rem /s — рекурсивно (со всем содержимым)
rem /q — без вопросов подтверждения

mkdir build

g++ main.cpp -Os -fno-declone-ctor-dtor -o build/main.exe

.\build\main.exe