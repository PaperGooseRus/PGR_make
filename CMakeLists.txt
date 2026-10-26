cmake_minimum_required(VERSION 3.16)
project(app CXX)

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

