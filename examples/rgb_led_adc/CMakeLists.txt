# Self-contained build for the rgb_led_adc example: the device + host demo
# executable and the optional Python bindings module. Included via
# add_subdirectory() from the top-level CMakeLists; uses the `eup` target and
# the test setup (enable_testing) defined there.

# --- C++ demo: device firmware + host driver sharing one command contract ---
add_executable(eup_example
    device.cpp
    main.cpp
)
target_link_libraries(eup_example PRIVATE eup)
target_include_directories(eup_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(eup_example PRIVATE /W4 /GR-)
else()
    target_compile_options(eup_example PRIVATE
        -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
endif()

# --- Optional Python bindings (pybind11), fetched automatically. Disabled with
#     -DEUP_BUILD_PYTHON=OFF, and skipped if no Python development headers exist.
option(EUP_BUILD_PYTHON "Build the pybind11 Python bindings" ON)
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

        # python_module.cpp registers this example's contracts; the generic
        # binding core lives in <repo>/bindings/eup_pybind.hpp.
        pybind11_add_module(eup_py python_module.cpp)
        set_target_properties(eup_py PROPERTIES OUTPUT_NAME eup)  # import eup
        target_link_libraries(eup_py PRIVATE eup)
        target_include_directories(eup_py PRIVATE
            ${CMAKE_SOURCE_DIR}/bindings        # eup_pybind.hpp (core)
            ${CMAKE_CURRENT_SOURCE_DIR})        # commands.hpp (app)

        add_test(NAME eup_python_tests
                 COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_python.py)
        set_tests_properties(eup_python_tests PROPERTIES
            ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:eup_py>")
    else()
        message(STATUS "Python development headers not found; skipping eup_py")
    endif()
endif()
