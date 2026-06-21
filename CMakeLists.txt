cmake_minimum_required(VERSION 3.16)
project(EmbeddedUnifiedProtocol CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The codec itself: portable, freestanding-friendly C++17.
add_library(eup STATIC
    src/crc16.cpp
    src/cobs.cpp
    src/frame.cpp
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
add_executable(eup_tests tests/test_protocol.cpp)
target_link_libraries(eup_tests PRIVATE eup)
add_test(NAME eup_tests COMMAND eup_tests)
