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
