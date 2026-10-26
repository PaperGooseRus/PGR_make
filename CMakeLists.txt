cmake_minimum_required(VERSION 3.16)
project(PGR_Make LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
    -Os
    -Wall
    -Wextra
)

add_compile_options(
    $<$<CXX_COMPILER_ID:GNU>:-fno-declone-ctor-dtor>
)

add_subdirectory(core)

add_executable(app
    main.cpp
)

target_link_libraries(app PRIVATE core)


# Static MinGW runtime for a portable Windows PGR_Make.exe.
option(PGR_STATIC_WINDOWS "Statically link GCC and libstdc++ for portable Windows builds" ON)
set_target_properties(app PROPERTIES OUTPUT_NAME "PGR_Make")
if(MINGW AND PGR_STATIC_WINDOWS)
    target_link_options(app PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# CommandLineToArgvW resides in Shell32; required for UTF-8 Windows CLI paths.
if(WIN32)
    target_link_libraries(app PRIVATE shell32)
endif()
