cmake_minimum_required(VERSION 3.16)
project(PGR_Make VERSION 0.8.0 LANGUAGES CXX)

option(PGR_ENABLE_SANITIZERS "Enable ASan/UBSan on GCC or Clang (Linux CI)" OFF)
if(PGR_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers require GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

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
        target_compile_definitions(pgr_gui PRIVATE PGR_APP_VERSION="${PROJECT_VERSION}")
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
    add_executable(pgr_gui_ux_test tests/test_gui_ux.cpp)
    add_test(NAME gui_ux_model COMMAND pgr_gui_ux_test "${CMAKE_CURRENT_BINARY_DIR}/pgr_gui_ux")
    add_executable(pgr_gui_layout_test tests/test_gui_layout.cpp)
    add_test(NAME gui_layout_preferences COMMAND pgr_gui_layout_test "${CMAKE_CURRENT_BINARY_DIR}/pgr_gui_layout_data/preferences.ini")
    add_executable(pgr_process_test tests/test_process.cpp)
    target_link_libraries(pgr_process_test PRIVATE core)
    add_test(NAME process_cancellation COMMAND pgr_process_test)
    # These tests use assert(); Release must not compile their checks away.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        foreach(unit pgr_diagnostics_test pgr_cmake_root_test pgr_workspace_test
                pgr_gui_ux_test pgr_gui_layout_test pgr_process_test)
            target_compile_options(${unit} PRIVATE -UNDEBUG)
        endforeach()
    elseif(MSVC)
        foreach(unit pgr_diagnostics_test pgr_cmake_root_test pgr_workspace_test
                pgr_gui_ux_test pgr_gui_layout_test pgr_process_test)
            target_compile_options(${unit} PRIVATE /UNDEBUG)
        endforeach()
    endif()
    # Independent unit suites for modules that previously had no direct tests.
    # They are included in CTest so failures block a release, not just printed.
    foreach(module hash json paths pack_schema config tool_resolution toolchain presets project_setup)
        add_executable(pgr_unit_${module} tests/units/test_${module}.cpp)
        target_link_libraries(pgr_unit_${module} PRIVATE core)
        add_test(NAME unit_${module} COMMAND pgr_unit_${module})
        set_tests_properties(unit_${module} PROPERTIES LABELS "unit;core" TIMEOUT 30)
    endforeach()
    # These old and new tests use assertions: ensure they remain active under -DNDEBUG.
    foreach(unit pgr_unit_hash pgr_unit_json pgr_unit_paths
                 pgr_unit_pack_schema pgr_unit_config pgr_unit_tool_resolution
                 pgr_unit_toolchain pgr_unit_presets pgr_unit_project_setup)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${unit} PRIVATE -UNDEBUG)
        elseif(MSVC)
            target_compile_options(${unit} PRIVATE /UNDEBUG)
        endif()
    endforeach()
    set_tests_properties(diagnostics_parsing cmake_project_root workspace_management
        gui_ux_model gui_layout_preferences process_cancellation
        PROPERTIES LABELS "unit;core" TIMEOUT 45)

    option(PGR_REQUIRE_PYTHON_TESTS "Fail when Python integration tests cannot be registered" OFF)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(PGR_REQUIRE_PYTHON_TESTS AND NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "Python3 is required to register full integration tests")
    endif()
    if(Python3_Interpreter_FOUND)
        foreach(test run_tests run_features run_gui_bridge run_gui_commands test_history)
            add_test(NAME ${test} COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${test}.py $<TARGET_FILE:app>)
            set_tests_properties(${test} PROPERTIES TIMEOUT 120)
        endforeach()
        add_test(NAME gui_surface_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui_surface.py)
        add_test(NAME gui_theme_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui_theme.py)
        add_test(NAME gui_v06_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui_v06.py)
        add_test(NAME gui_v07_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui_v07.py)
        # The legacy ZIP fixture uses POSIX shell wrappers; Windows gets its own
        # native Ninja ZIP/PowerShell importer test instead.
        if(NOT WIN32)
            add_test(NAME pack_manager_regression COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pack_manager.py $<TARGET_FILE:app>)
            set_tests_properties(pack_manager_regression PROPERTIES TIMEOUT 120)
        endif()
        foreach(t run_tests run_features run_gui_bridge run_gui_commands test_history)
            set_tests_properties(${t} PROPERTIES LABELS "integration;cli")
        endforeach()
        if(NOT WIN32)
            set_tests_properties(pack_manager_regression PROPERTIES LABELS "integration;cli")
        endif()
        foreach(t gui_surface_contract gui_theme_contract gui_v06_contract gui_v07_contract)
            set_tests_properties(${t} PROPERTIES LABELS "contract;gui")
        endforeach()
        add_test(NAME acceptance_matrix COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/acceptance_matrix.py
            $<TARGET_FILE:app>)
        set_tests_properties(acceptance_matrix PROPERTIES LABELS "integration;acceptance" TIMEOUT 180)
        if(WIN32)
            add_test(NAME pack_windows_acceptance COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/pack_windows.py
                $<TARGET_FILE:app>)
            set_tests_properties(pack_windows_acceptance PROPERTIES LABELS "integration;acceptance" TIMEOUT 180)
        endif()

    endif()
endif()
