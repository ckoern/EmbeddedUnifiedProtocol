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

# Example: device firmware + host driver sharing one command contract.
add_executable(eup_example
    examples/rgb_led_adc/device.cpp
    examples/rgb_led_adc/main.cpp
)
target_link_libraries(eup_example PRIVATE eup)
target_include_directories(eup_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/rgb_led_adc)
if(MSVC)
    target_compile_options(eup_example PRIVATE /W4 /GR-)
else()
    target_compile_options(eup_example PRIVATE
        -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
endif()
