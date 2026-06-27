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

# --- Python bindings for this example's contracts. Reuses the shared pybind11
#     setup from the top-level CMakeLists (EUP_PYTHON_ENABLED, Python_EXECUTABLE).
if(EUP_PYTHON_ENABLED)
    # python_module.cpp registers this example's contracts; the generic binding
    # core lives in <repo>/bindings/eup_pybind.hpp.
    pybind11_add_module(example_protocol_py python_module.cpp)
    set_target_properties(example_protocol_py PROPERTIES OUTPUT_NAME example_protocol)
    target_link_libraries(example_protocol_py PRIVATE eup)
    target_include_directories(example_protocol_py PRIVATE
        ${CMAKE_SOURCE_DIR}/bindings        # eup_pybind.hpp (core)
        ${CMAKE_CURRENT_SOURCE_DIR})        # commands.hpp (app)

    add_test(NAME example_protocol_python_tests
             COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_python.py)
    set_tests_properties(example_protocol_python_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:example_protocol_py>")
endif()
