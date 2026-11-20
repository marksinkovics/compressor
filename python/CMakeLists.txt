project(ingot_python)

# vcpkg's python3 port, which pybind11 depends on; the module imports only into that version.
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(ingot_module MODULE src/ingot.cpp)
set_target_properties(ingot_module PROPERTIES
    OUTPUT_NAME ingot
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package"
    FOLDER python
)
target_link_libraries(ingot_module PRIVATE compressor)
target_compile_definitions(ingot_module PRIVATE COMPRESSOR_VERSION="${CMAKE_PROJECT_VERSION}")

add_test(NAME Python.Module
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/test/test_ingot.py"
        "${CMAKE_CURRENT_BINARY_DIR}/package"
)

# A wheel of the module after every build (tools/python_wheel.py, standard library only).
add_custom_command(TARGET ingot_module POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/dist"
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/python_wheel.py"
        --module "$<TARGET_FILE:ingot_module>" --python "${Python3_EXECUTABLE}"
        --version "${CMAKE_PROJECT_VERSION}" --out "${CMAKE_CURRENT_BINARY_DIR}/dist"
    VERBATIM
)

add_test(NAME Python.Wheel
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/test/install_wheel.py"
        "${CMAKE_CURRENT_BINARY_DIR}/dist" "${CMAKE_CURRENT_BINARY_DIR}/wheel-venv"
)
