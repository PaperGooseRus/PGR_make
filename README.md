# PGR_make: сборка C/C++ через TUI и CMake

PGR_make — небольшая утилита на **C++17**. Поддерживает интерактивное терминальное меню (TUI), прямой вызов GCC/Clang и автоматическое построение **временного проекта CMake** для однокомандной сборки. Подходит для простых проектов ПК; базовые параметры cross-compilation задаются конфигурацией. Проект намеренно не требует GTK4, Qt, Python или сторонних C++ библиотек.

## Быстрый запуск

**Windows, MSYS2 UCRT64 (GCC и CMake):**

```bat
main.bat
```

Или из MSYS2 UCRT64 / Linux/macOS:

```sh
g++ -std=c++17 -O2 -Wall -Wextra main.cpp -o pgr_make
./pgr_make
```

Штатный собственный CMake-проект:

```sh
cmake -S . -B build-review
cmake --build build-review
```

В Windows вместо `./pgr_make` запускайте `pgr_make.exe`.

**CI/headless**, из корня проекта:

```sh
./pgr_make --ci --clean
./pgr_make --ci --profile debug --build-dir build-debug
```

`--profile NAME` читает `project/profiles/NAME.cfg`. Допустимы буквы, цифры, `_` и `-`. Каталог `project/generated_cmake` и файл `project/config.cfg` должны быть доступны для записи. Чтобы запустить сборку **другого** проекта, перейдите в его корневой каталог, создайте там `project/config.cfg`, затем вызывайте `pgr_make` из этого каталога.

## Конфигурация

Настройки хранятся в `project/config.cfg` (или `project/profiles/*.cfg`). Формат: `ключ=значение`; многозначные ключи повторяются.

```ini
compiler=g++
source_files=main.cpp
source_files=src/util.cpp
output_file=build/app
standard=-std=c++17
c_standard=-std=c11
optimization=-O2
warnings=-Wall
warnings=-Wextra
include_dirs=include
library_dirs=third_party/lib
libraries=pthread
sanitizers=-fsanitize=address
profile=debug
# Для cross-compilation:
# toolchain_file=cmake/arm-none-eabi.cmake
# linker_script=linker.ld
# target_type=shared
```

При сборке через CMake поддерживаются компиляторы C и C++, `find_package` (ключ `packages`, запись `Package|Package::Target`), include-пути, библиотеки, санитайзеры при компиляции **и** компоновке, GCC/Clang-специфичные опции, `-T linker.ld`, toolchain-файлы и `target_type=static/shared` для целей-библиотек. Выбор типа библиотеки по умолчанию `static`; цель с `main()` считается исполняемым файлом. Одновременно выбранные несовместимые санитайзеры и некорректные флаги не допускаются.

При прямой компиляции используется один вызов компилятора для всех указанных `source_files`; `toolchain_file` относится **только к CMake**, а `packages` обрабатываются только CMake. Для проекта на C используйте `compiler=gcc`, для C++ обычно `compiler=g++`. При смешанном C/C++ предпочтительна сборка через CMake.

## Безопасность исходников и выполнение команд

- Все компиляторы и `cmake` запускаются **без оболочки**: на Windows через `CreateProcessW`, на POSIX через `fork/execvp`. Аргументы передаются отдельно, в том числе пути с пробелами.
- CI и пункт генерации в TUI создают проект в `project/generated_cmake`. Исходные `CMakeLists.txt` в рабочем проекте **не перезаписываются**.
- При очередной генерации каталог `project/generated_cmake` пересоздаётся. Не храните в нём собственные файлы.
- Параметр `--clean` применяется **только с `--ci`**, только внутри дерева проекта, только к папкам с именем `build`, `build_cmake`, `out`, `build-*`, `build_*`, `build ...` либо `cmake-build-debug/release`. Удаление корня, родительских каталогов и путей за пределами проекта запрещено. Ссылки вне дерева также отклоняются.
- Журнал TUI `project/work.log` содержит выполненные команды, их коды завершения и вывод компилятора/CMake.
- CI переносит `compile_commands.json` в корень, если CMake смог его сформировать.

## Структура исходников

| Файл | Ответственность |
| --- | --- |
| `main.cpp` | CLI, CI, вход в TUI |
| `core/TUI.hpp` | Интерактивное текстовое меню |
| `core/make.hpp` | Конфигурация, каталог опций, прямой режим |
| `core/tree.hpp` | Поиск исходников, эвристический анализ модулей |
| `core/cmake_layout.hpp` | План CMake-целей и подкаталогов |
| `core/cmake_writer.hpp` | Генерация CMake, безопасный staging |
| `core/process_runner.hpp` | Запуск процессов без shell, захват вывода |
| `core/safe_paths.hpp` | Проверка путей и безопасная очистка |
| `tests/run_tests.py` | Регрессионные интеграционные проверки |

## Регрессионные тесты

Для автоматических тестов нужен Python 3.8+, GCC/G++ и CMake:

```sh
python tests/run_tests.py ./pgr_make
```

Тесты создают временные проекты и проверяют C, C++, путь с пробелом, санитайзер, include-пути, #finder, циклические заголовки, shared library, специальные GCC-флаги, неизменность исходного `CMakeLists.txt`, защиту `--clean`, пустой конфиг и некорректные флаги.

## План развития и проверяемые этапы

1. **Базовая сборка.** Проверить собственный `cmake -S . -B build-review` и сборку простого C++ файла.
2. **Конфигурация и безопасность.** Проверить пустой/неправильный конфиг, пути с пробелами, безопасность `--clean`.
3. **CMake и зависимости.** Проверить C/C++, include, санитайзеры, разделение библиотек и приложения.
4. **Кросс-компиляция.** Для конкретного MCU добавить реальный toolchain-файл, linker script и тест ELF, с проверкой используемого компилятора.
5. **Стабилизация.** Запускать `tests/run_tests.py` после каждого изменения и проверять журнал TUI.

## Известные границы возможностей

Автоматическое распознавание модулей и зависимостей **эвристическое**, это не полноценный C/C++ препроцессор и не заменяет явное описание CMake-целей сложного проекта. Директива флагов распознаётся только в комментарии вида `// #finder -Wall -Iinclude -lm`, не в произвольном тексте документации. Проекты на C и C++ собираются; `asm`, `CUDA`, `Rust`, полноценный поиск пакетов по `pkg-config`, настройка CMake Presets и post-build-конвертация `.elf` в `.bin`/`.hex` пока не реализованы. Внешние include-пути указывайте явно через `include_dirs`.

Проверки сборки выполнены с GCC 14.2, CMake и Ninja в Linux. **Нативная сборка/запуск на Windows и реальные ARM/RISC-V toolchain-файлы не проверялись**: проект предназначен для Windows/MSYS2, но эти платформенные сценарии нужно проверить на целевой машине.
