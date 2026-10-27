cmake_minimum_required(VERSION 3.16)
project(PGR_Make LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GCC/Clang flags must never be passed to MSVC cl.exe.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Os -Wall -Wextra)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fno-declone-ctor-dtor)
endif()

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

# GTK4 GUI is a separate executable. The CLI remains usable on machines
# without GTK4 development headers/libraries.
option(PGR_BUILD_GUI "Build GTK4 graphical frontend when available" ON)
option(PGR_REQUIRE_GUI "Fail configuration if GTK4 is unavailable" OFF)
if(PGR_BUILD_GUI)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK4 QUIET IMPORTED_TARGET gtk4>=4.10)
    endif()
    if(GTK4_FOUND)
        add_executable(pgr_gui gui/main.cpp)
        target_link_libraries(pgr_gui PRIVATE core PkgConfig::GTK4)
        set_target_properties(pgr_gui PROPERTIES OUTPUT_NAME "PGR_Make_GUI")
        if(WIN32)
            set_target_properties(pgr_gui PROPERTIES WIN32_EXECUTABLE TRUE)
        endif()
        if(MINGW AND PGR_STATIC_WINDOWS)
            # GTK4 uses DLLs in MSYS2. Only the C++/GCC runtime is linked
            # statically here; do NOT force -static on the GTK libraries.
            target_link_options(pgr_gui PRIVATE -static-libgcc -static-libstdc++)
        endif()
    elseif(PGR_REQUIRE_GUI)
        message(FATAL_ERROR "GTK4 development files not found (pkg-config gtk4). Install GTK4 or disable PGR_REQUIRE_GUI.")
    else()
        message(WARNING "GTK4 development files not found; building CLI only. Set PGR_REQUIRE_GUI=ON to require GUI.")
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(pgr_diagnostics_test tests/test_diagnostics.cpp)
    add_test(NAME diagnostics_parsing COMMAND pgr_diagnostics_test)
    # Regression: GUI starts from C:/PGR while project sources live elsewhere.
    add_executable(pgr_cmake_root_test tests/test_cmake_project_root.cpp)
    target_link_libraries(pgr_cmake_root_test PRIVATE core)
    add_test(NAME cmake_project_root COMMAND pgr_cmake_root_test "${CMAKE_CURRENT_BINARY_DIR}/pgr_project_root_test")
    add_executable(pgr_workspace_test tests/test_workspace.cpp)
    target_link_libraries(pgr_workspace_test PRIVATE core)
    add_test(NAME workspace_management COMMAND pgr_workspace_test)
    add_executable(pgr_process_test tests/test_process.cpp)
    target_link_libraries(pgr_process_test PRIVATE core)
    add_test(NAME process_cancellation COMMAND pgr_process_test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        foreach(test run_tests run_features run_gui_bridge run_gui_commands test_history)
            add_test(NAME ${test} COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${test}.py $<TARGET_FILE:app>)
            set_tests_properties(${test} PROPERTIES TIMEOUT 120)
        endforeach()
        add_test(NAME gui_surface_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui_surface.py)
        add_test(NAME pack_manager_regression COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pack_manager.py $<TARGET_FILE:app>)
        set_tests_properties(pack_manager_regression PROPERTIES TIMEOUT 120)
    endif()
endif()
