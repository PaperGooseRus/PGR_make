# PGR_Make 0.4 — отчёт о проверках

Запуск: `cmake -S . -B build -G Ninja -DPGR_BUILD_GUI=OFF`, затем `cmake --build build` и `ctest --test-dir build --output-on-failure`.

| Группа CTest | Содержание | Linux |
|---|---|---|
| diagnostics_parsing | GCC/Clang/MSVC/clangd, JSON сохранение/чтение | PASS |
| workspace_management | Переименование, клон, перенос, недоступные исходники, архив, сохранность файлов | PASS |
| process_cancellation | Отмена процесса и потомков | PASS |
| run_tests | Сборки C/C++, CMake, флаги, очистка, Pack, профили, TUI | PASS |
| run_features | Presets, cross-toolchain, OBJECT, MSVC mock, clangd, objcopy | PASS |
| run_gui_bridge | Сохранность API GUI/CLI | PASS |
| run_gui_commands | Вспомогательные команды и каталог сборки | PASS |
| test_history | История прямой и CMake-сборки | PASS |
| gui_surface_contract | Текстовые проверки интерфейсного исходника | PASS |

Данные проверки НЕ эквивалентны нативной компиляции и запуску настоящего GTK4 GUI. В данном окружении нет GTK4 SDK и Windows toolchain.

Оставшиеся риски: MSVC Developer Command Prompt/vcvarsall, GTK4 runtime на Windows, настоящие GUI-тесты, состояние интерфейса после длительных сборок под Windows и отдельные редкие случаи диагностики компиляторов.
