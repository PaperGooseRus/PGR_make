# PGR_Make — проверка расширенной версии

Дата: 26.10.2026.

Проверено в Linux, GCC 14.2, CMake 3.31, Ninja, clangd 17.0.

Команды:

```sh
cmake -S . -B build-review -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-review
ctest --test-dir build-review --output-on-failure
python3 tests/run_tests.py build-review/PGR_Make
python3 tests/run_features.py build-review/PGR_Make
```

- Пройдены **32 исходных регрессионных сценария**.
- Пройден **модульный тест парсера** (GCC, MSVC C2065/LNK2019, clangd, warning -Wcode, cross prefix).
- Пройдены **14 новых функциональных сценариев** (CMakePresets, Conan/vcpkg config validation, успешный/ошибочный JSON, линковщик, OBJECT, cross-prefix/objcopy ELF/BIN/HEX, аргументы MSVC через имитатор, clangd-диагностика защита BIN и восстановление записей TUI).
- Проверен отказ от небезопасной очистки и сохранность пользовательских файлов в исходном наборе.

**Ограничения:** готовый Windows EXE, реальный компилятор MSVC, реальные ARM/RISC-V и установленный Conan/vcpkg здесь не проверены. Тест cross-prefix использует имитаторы имён компиляторов, перенаправляющие команды на системный GCC. Генерация BIN/HEX проверена при помощи системного `objcopy`. `clangd --check` реализован как одноразовый анализ, полноценный LSP-сервер/клиент не реализован.
