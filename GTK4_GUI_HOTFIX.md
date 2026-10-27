# Исправление сборки GTK4 GUI в PGR_Make 0.5

**Источник ошибки:** `gui/main.cpp`, строковые литералы в окне Pack Manager.
Переносы строк в двух вызовах `label()` были записаны как физические переносы между кавычками, что нарушает синтаксис C++17.

**Исправлено:** в обоих случаях используется `\n` внутри строкового литерала.
В `tests/test_gui_surface.py` добавлены регрессионные проверки.
Функциональность Pack Manager не изменена.

**Сборка Windows (MSYS2 UCRT64):**

```sh
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DPGR_STATIC_WINDOWS=ON -DPGR_BUILD_GUI=ON -DPGR_REQUIRE_GUI=ON
cmake --build build-win --parallel
```

Если исходники заменены в уже настроенном проекте, повторный `cmake -S` обычно не требуется, можно выполнить `cmake --build build-win`.

**Проверки:** тесты ядра/CLI и контрактный тест GUI в Linux. Полноценная компиляция с GTK4 под Windows данным окружением не выполнена.
