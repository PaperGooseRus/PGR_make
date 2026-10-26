#ifndef PGR_BUILTIN_FLAGS_HPP
#define PGR_BUILTIN_FLAGS_HPP
#include <string>
namespace pgr_builtin {
inline const char* flags(const std::string& name) {
    if (name == "arch") return R"PGRFLAGS(-march=native|Оптимизация под текущий процессор (максимальная производительность)
-march=x86-64|Базовый x86-64 (совместимость с большинством современных CPU)
-march=x86-64-v2|Требует SSE4.2, POPCNT
-march=x86-64-v3|Требует AVX2, BMI2 и другие (не работает на старых CPU)
-mtune=native|Настройка планировщика инструкций под текущий CPU (без изменения набора))PGRFLAGS";
    if (name == "compiler") return R"PGRFLAGS(# Компиляторы. Один на строку.
gcc|GNU C Compiler (C)
g++|GNU C++ Compiler (C++)
clang|LLVM Clang (C/C++)
clang++|LLVM Clang (C++))PGRFLAGS";
    if (name == "debug") return R"PGRFLAGS(-g|Стандартная отладочная информация
-g3|Максимальный уровень отладочной информации
-ggdb|Специально для GDB (максимально подробно))PGRFLAGS";
    if (name == "lto") return R"PGRFLAGS(-flto|Link-Time Optimization (межмодульная оптимизация)
-flto=thin|ThinLTO (быстрее, чем полный LTO, только Clang/LLVM, не GCC))PGRFLAGS";
    if (name == "optimization") return R"PGRFLAGS(-O0|Без оптимизации (быстрая компиляция, удобная отладка)
-O1|Базовая оптимизация (быстрее, чем -O0)
-O2|Стандартная оптимизация (баланс скорости и размера)
-O3|Агрессивная оптимизация (максимальная скорость, больший размер)
-Os|Оптимизация по размеру (уменьшает размер кода)
-Ofast|Игнорирует строгие стандарты ради скорости)PGRFLAGS";
    if (name == "sanitizers") return R"PGRFLAGS(-fsanitize=address|AddressSanitizer: ошибки работы с памятью (use-after-free и т.п.)
-fsanitize=undefined|UndefinedBehaviorSanitizer: неопределённое поведение
-fsanitize=leak|LeakSanitizer: утечки памяти (несовместим с thread)
-fsanitize=thread|ThreadSanitizer: гонки данных (несовместим с address, leak, memory)
-fsanitize=memory|MemorySanitizer: неинициализированная память (только Clang, несовместим с address)
-fno-omit-frame-pointer|Сохраняет указатель стека (улучшает отчёты санитайзеров)
-fno-sanitize-recover=all|Останавливаться при первой ошибке санитайзера)PGRFLAGS";
    if (name == "specific_clang") return R"PGRFLAGS(-stdlib=libc++|Использовать стандартную библиотеку libc++ вместо libstdc++ (только Clang)
-fcolor-diagnostics|Цветной вывод диагностики (только Clang)
-Wdeprecated-non-prototype|Предупреждение об устаревших объявлениях функций (только Clang))PGRFLAGS";
    if (name == "specific_gcc") return R"PGRFLAGS(-fno-declone-ctor-dtor|Обход бага GCC 16 (GCC Bug 125359)
-fipa-pure-const|Межпроцедурный анализ чистых функций (только GCC)
-fipa-cp-clone|Клонирование функций при межпроцедурной оптимизации (только GCC)
-fhardened|Включает набор защитных механизмов (только новые GCC)
-fanalyzer|Статический анализатор кода (только GCC))PGRFLAGS";
    if (name == "standard") return R"PGRFLAGS(-std=c++11|C++11 (устаревший)
-std=c++14|C++14
-std=c++17|C++17 (поддерживает filesystem)
-std=c++20|C++20 (concepts, ranges, coroutines)
-std=c++23|C++23 (новейший, частичная поддержка))PGRFLAGS";
    if (name == "warnings") return R"PGRFLAGS(-Wall|Включает основной набор предупреждений
-Wextra|Дополнительные предупреждения (выходят за рамки -Wall)
-Wpedantic|Строгое соответствие стандарту ISO C++
-Wshadow|Предупреждает о перекрытии имён переменных
-Wconversion|Предупреждает о неявных преобразованиях типов
-Wsign-conversion|Предупреждает о преобразованиях со знаком/без знака
-Wcast-qual|Предупреждает об опасных приведениях типов
-Wold-style-cast|Предупреждает о C-подобных приведениях типов
-Wnon-virtual-dtor|Предупреждает о невиртуальных деструкторах
-Woverloaded-virtual|Предупреждает о перекрытии виртуальных функций
-Wnull-dereference|Предупреждает о разыменовании nullptr
-Wdouble-promotion|Предупреждает о неявном повышении float до double
-Wformat=2|Строгая проверка форматов printf/scanf
-Werror|Превращает все предупреждения в ошибки)PGRFLAGS";
    return "";
}
}
#endif
