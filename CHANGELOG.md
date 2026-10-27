## 0.8.1 — Pack Manager: bulk install

- Одна кнопка установки последних проверенных GCC/Clang, CMake, Ninja из каталога.
- Общий SHA-256-проверенный WinLibs UCRT ZIP для GCC и Clang, единая атомарная регистрация и безопасный карантин.
- Числовая сортировка версий, пропуск установленных, обработка отсутствующих/повреждённых пакетов.
- Слияние встроенного и пользовательского каталогов для корректных обновлений.
- Дополнительные модульные и интеграционные тесты, обновление скриптов доставки Windows.

## GTK4 Ash & Orange · обновление оформления (2026-10-27)

- Новая тёмная пепельно-графитовая тема с терракотовым акцентом; опциональная светлая тема.
- Цвета и CSS вынесены в `gui/theme.hpp`, встроены в приложение.
- Упрощена основная панель действий, оформлены секции, проектная панель, результаты и диагностика.
- Запоминается тема отдельно от конфигураций проектов; добавлен индикатор процесса сборки.
- Новый регрессионный тест `tests/test_gui_theme.py` и руководство `GUI_THEME.md`.

## 0.4 — BuildController и GTK4 UX

- Shared BuildController, explicit tool resolution, cancellable process tree, CLI/GUI parity.
- Registry rename, relocate, clone, archive and offline display.
- GUI quick-build selector, project/settings search, dirty guard, diagnostic filters and history.
- Strict diagnostics JSON parser, isolated append-only history, MSVC-safe top-level CMake options.
- New native and integration tests. Native GTK4 Windows execution remains unverified.

## GTK4 GUI 0.2 — 2026-10-26

- Горизонтальный GtkPaned: все настройки и команды слева, список проектов справа.
- GTK4 GtkDropDown, множественные чекбоксы, многострочные списки путей и GtkFileDialog.
- Новое поле package_dirs для CMAKE_PREFIX_PATH и выбора *Config.cmake.
- Неинтерактивные CLI-действия --show-command, --generate-only, --clean-only, --export-commands.
- Безопасный экспорт базы команд без перезаписи, отдельные сохранение/загрузка профилей.

## GTK4 GUI 0.1

- Отдельный `PGR_Make_GUI` на GTK4; сохранён исходный CLI/TUI.
- Менеджер проектов, диалог создания проекта, выбор компилятора из Pack, настройка Debug/Release, три режима сборки.
- Неблокирующее отображение журнала, диагностик, команда анализа clangd, открытие исходника в редакторе.
- Общий модуль `core/project_setup.hpp` для регистрации и первичной настройки проекта.
- Новый CLI-флаг `--direct` для вызова прямого backend без TUI.
- Сборка GUI через CMake/pkg-config с проверкой доступности GTK4, Windows-скрипты и тест GUI/CLI контракта.
- GUI 0.1 не является статическим GTK4 EXE; runtime GTK4 поставляется отдельно.

# История изменений

## Исправления стабильности и безопасности (26 октября 2026)

- Защита очистки маркерами принадлежности: чужие файлы не удаляются.
- Изоляция бинарных каталогов: `.pgr_build/<имя>` для каждой записи проекта.
- Защита каталога сгенерированного CMake от удаления неизвестных файлов.
- Отдельная компиляция исходников C в прямой смешанной C/C++-сборке.
- Автоматическая регенерация CMake в TUI перед каждой сборкой.
- Пересоздание **только своего** CMake-кеша при изменении компилятора/toolchain.
- Сборка существующих CMake-проектов (`--native-cmake`, TUI п.32).
- Обнаружение инструментов Pack (`--toolchains`, TUI п.31) и сохранение абсолютного пути компилятора.
- Рекурсивное обнаружение новых исходников C/C++ и выбор GCC C для чистого C.
- Сохранение compile_commands.json в хранилище проекта без перезаписи чужого файла.
- Корректное декодирование Unicode-аргументов командной строки Windows (CommandLineToArgvW).
- Дополнительные регрессионные сценарии.


## PGR_Make — переносимый менеджер проектов (26 октября 2026)

- Переносимый корень определяется по расположению `PGR_Make.exe`, а не по текущей папке терминала.
- Автоматически создаются `<app>/Pack` и `<app>/Project`.
- Добавлен интерактивный реестр: выбор зарегистрированного проекта, создание по имени и директории, пункт 30 для переключения.
- Настройки/профили/журнал/сгенерированный CMake каждого проекта изолированы в `<app>/Project/<name>`.
- Каталог исходников может располагаться на другом диске и содержать пробелы.
- Добавлены CLI `--list`, `--create`, `--source-dir`, `--starter`, `--project`, `--home`.
- Автоматическая регистрация `project.pgr`, начальной конфигурации, возможность создания стартового `main.cpp`.
- Встроены каталоги стандартных флагов (`builtin_flags.hpp`): программа не зависит от соседней папки config.
- Поиск GCC, Clang, CMake, Ninja и других инструментов внутри Pack, добавление bin-папок перед PATH.
- Генерация CMake выполняется в центральном хранилище выбранного проекта, без замены исходных CMakeLists.txt.
- Добавлена опциональная статическая линковка MinGW (`PGR_STATIC_WINDOWS`) и скрипты сборки/установки в C:\\PGR.
- Обновлены регрессионные тесты и документация.

Тестирование: Linux GCC 14.2, CMake, Ninja; Windows-бинарник не собран в Linux-среде без кросс-компилятора.

## 2026-10-26: toolchain abstraction and diagnostics

- Added `core/diagnostics.hpp`, structured compiler/linker/clangd diagnostics, saved JSON, TUI browser and VS Code goto.
- Added `core/analysis.hpp`: on-demand clangd check using compile_commands.json (not a persistent LSP client).
- Added `core/presets.hpp` with isolated CMakePresets, vcpkg VCPKG_ROOT validation and opt-in Conan toolchain handling.
- Added optional MSVC command-line backend and compiler family detection; expanded Pack `cl.exe` discovery.
- Added cross compiler prefix, objcopy executable and optional ELF to BIN/HEX post-build in `core/artifacts.hpp`.
- Added generated OBJECT targets and selected header-only INTERFACE targets; preserved prior static/shared defaults.
- Added CLI options `--diagnostics`, `--analyze`, `--write-presets` and TUI commands 33-38.
- Added CTest diagnostics parser, feature regression suite, architecture/status document.
- Windows/MSVC native builds, Conan/vcpkg installations and live LSP sessions are not claimed as validated.
