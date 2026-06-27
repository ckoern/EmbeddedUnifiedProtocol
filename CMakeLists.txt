cmake_minimum_required(VERSION 3.16)
project(EmbeddedUnifiedProtocol CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit build/compile_commands.json so editors/IntelliSense pick up exact flags
# and include paths (including the fetched Catch2 headers).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The codec itself: portable, freestanding-friendly C++17.
add_library(eup STATIC
    src/crc16.cpp
    src/cobs.cpp
    src/frame.cpp
    src/command.cpp
)
target_include_directories(eup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Embedded subset: no exceptions, no RTTI. Drop these flags if your toolchain
# needs them, but the code does not rely on either feature.
if(MSVC)
    target_compile_options(eup PRIVATE /W4 /GR-)
else()
    target_compile_options(eup PRIVATE
        -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
endif()

enable_testing()

# Catch2 v3 is fetched automatically at configure time (or reused if already
# installed). The test executables link Catch2WithMain; the library itself has
# no test dependencies.
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.15.1
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
include(Catch)  # provides catch_discover_tests()

add_executable(eup_tests tests/test_protocol.cpp)
target_link_libraries(eup_tests PRIVATE eup Catch2::Catch2WithMain)
catch_discover_tests(eup_tests)

add_executable(eup_command_tests tests/test_command.cpp)
target_link_libraries(eup_command_tests PRIVATE eup Catch2::Catch2WithMain)
catch_discover_tests(eup_command_tests)

add_executable(eup_host_tests tests/test_host.cpp)
target_link_libraries(eup_host_tests PRIVATE eup Catch2::Catch2WithMain)
catch_discover_tests(eup_host_tests)

add_executable(eup_stream_tests tests/test_stream.cpp)
target_link_libraries(eup_stream_tests PRIVATE eup Catch2::Catch2WithMain)
catch_discover_tests(eup_stream_tests)

# Python bindings infrastructure, shared by the core-library module here and the
# example modules. Fetched automatically; disabled with -DEUP_BUILD_PYTHON=OFF,
# and skipped if no Python development headers are found.
option(EUP_BUILD_PYTHON "Build the pybind11 Python bindings" ON)
set(EUP_PYTHON_ENABLED OFF)
if(EUP_BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development QUIET)
    if(Python_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG        v2.13.6
        )
        FetchContent_MakeAvailable(pybind11)
        set(EUP_PYTHON_ENABLED ON)
    else()
        message(STATUS "Python development headers not found; skipping Python bindings")
    endif()
endif()

# Core-library Python bindings: CRC16, COBS, framing (the `eup` module).
if(EUP_PYTHON_ENABLED)
    pybind11_add_module(eup_py bindings/eup_core_module.cpp)
    set_target_properties(eup_py PROPERTIES OUTPUT_NAME eup)  # import eup
    target_link_libraries(eup_py PRIVATE eup)
    target_include_directories(eup_py PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bindings)
    add_test(NAME eup_python_tests
             COMMAND ${Python_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/bindings/test_eup.py)
    set_tests_properties(eup_python_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:eup_py>")
endif()

# Examples build themselves (each is self-contained and uses the `eup` target and
# the Python setup above).
add_subdirectory(examples/rgb_led_adc)
