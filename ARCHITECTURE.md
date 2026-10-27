# PGR_Make: архитектура и статус реализации

## Назначение

Переносимый менеджер проектов C/C++, хранящий конфигурации в `<PGR_HOME>/Project/<name>`, инструменты в `<PGR_HOME>/Pack`, а исходники в произвольных папках. База: C++17 и CMake 3.16+.

## Модули и границы

- `core/workspace.hpp`: реестр проектов, пути и обнаружение исходников.
- `core/pack_tools.hpp`: обнаружение GCC, Clang, MSVC в `Pack`; поиск исполняемых файлов в PATH.
- `core/toolchain.hpp`: классификация семейств компиляторов, C/C++ пары, cross-prefix.
- `core/make.hpp`: сериализация конфигурации и непосредственная сборка. Есть отдельный минимальный MSVC backend для `cl`/`cl.exe`.
- `core/tree.hpp`, `cmake_layout.hpp`, `cmake_writer.hpp`, `cmake_session.hpp`: эвристика CMake-целей, генерация и контроль CMake-кеша.
- `core/presets.hpp`: создание Presets и передача toolchain для vcpkg/Conan. Оригинальные `CMakeLists.txt` и `CMakePresets.json` не меняются.
- `core/process_runner.hpp`: запуск отдельных процессов с массивом аргументов, без командной оболочки.
- `core/diagnostics.hpp`: парсинг GCC/Clang/MSVC/clangd в единый формат; сохранение и загрузка JSON.
- `core/analysis.hpp`: однократный `clangd --check` для исходников, имеющих compile_commands.json.
- `core/artifacts.hpp`: опциональное преобразование ELF в BIN/HEX через objcopy.
- `core/safe_paths.hpp`: запрет очистки чужих каталогов, маркировка каталогов сборки.
- `core/TUI.hpp`: UI и сценарии работы; разделение интерфейса и ядра сохраняется.

## Формат диагностик

`<PGR_HOME>/Project/<name>/diagnostics.json` содержит JSON-объект вида:

```json
{
  "version": 1,
  "diagnostics": [
    {
      "severity": "error",
      "category": "symbol",
      "file": "src/example.cpp",
      "line": 12,
      "column": 3,
      "code": "",
      "message": "identifier was not declared",
      "tool": "GCC/Clang"
    }
  ]
}
```

Поля `code` и `column` могут отсутствовать в сыром сообщении (в JSON они тогда пустые или нулевые). Возможные категории: `compiler`, `syntax`, `symbol`, `include`, `template`, `standard_library`, `linker`. Парсер эвристический и не гарантирует понимание всех форматов/локализаций сообщений. Сырой вывод не теряется: для TUI пишется в `work.log`; CI дополнительно сохраняет процессный вывод туда же.

## Интеграция пакетов

- vcpkg: `package_manager=vcpkg` и `VCPKG_ROOT` должны указывать на установленную копию. PGR добавляет CMake toolchain во время configure и в свой Presets. Для отдельного embedded toolchain одновременно с vcpkg пока необходима ручная настройка chainload, автоматический chainload не реализован.
- Conan 2: `conan install` пользователь запускает отдельно. Для CMake-настроек укажите `package_manager=conan` и `toolchain_file=<путь к сгенерированному conan_toolchain.cmake>`. PGR не изменяет/не перезаписывает presets Conan в исходном дереве.
- Установку, обновление и разрешение версий зависимостей PGR не реализует. Это задача самих пакетных менеджеров.

## MSVC

`cl.exe` требует корректного окружения Visual Studio (например, Developer Command Prompt, `vcvarsall.bat` выполнен заранее). PGR **не** вызывает `vcvarsall` автоматически. Для прямой сборки поддержаны C/C++ исходники, `/std:c++NN`, базовые `/O*`, `/Zi`, `/I`, `/LIBPATH:` и `.lib`. Расширенные параметры компиляции DLL/PDB, платформенные manifest-флаги, MSBuild и платформозависимые конфигурации требуют дальнейшей разработки. На Linux тестировалась только генерация аргументов под эмулятор `cl`, настоящая сборка MSVC на Windows не проверена.

## clangd

Реализован **однократный режим проверки** через `clangd --check`, а **не** постоянный JSON-RPC LSP-клиент. Нужны установленный `clangd` и compilation database из стандартного CMake build-dir проекта. Запуск после провалившейся сборки тоже возможен, если конфигурация CMake успела создать базу. Интерактивная инкрементальная LSP-сессия и live diagnostics пока не реализованы.

## План проверки и развития (4 этапа)

1. **Базовая сборка и совместимость**: чистая сборка, прежние регрессионные тесты, загрузка старых конфигураций.
2. **Диагностика**: модульные тесты строк GCC/Clang/MSVC/clangd, ошибка компиляции/линковки и JSON, TUI-переоткрытие.
3. **Инструменты и CMake**: тест OBJECT и INTERFACE, корректные Presets vcpkg/Conan, изоляция файлов, cross-prefix и objcopy.
4. **Целевые платформы**: физическая Windows MSVC/MinGW и реальные ARM/RISC-V тулчейны, отладка путей/кодировок, развитие LSP и профильных backend-адаптеров.

Первые три этапа проверены в Linux-тестовой среде, четвёртый требует внешних инструментов и Windows-среды.
