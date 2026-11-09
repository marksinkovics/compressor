cmake_minimum_required(VERSION 3.25)
enable_testing()

project(CompressionProject CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# -std=c++23, not gnu++23, for the cxx_std_23 our libraries require.
set(CMAKE_CXX_EXTENSIONS OFF)

set(VERSION_MAJOR 0 CACHE STRING "Project major version number.")
set(VERSION_MINOR 0 CACHE STRING "Project minor version number.")
set(VERSION_PATCH 1 CACHE STRING "Project patch version number.")
mark_as_advanced(VERSION_MAJOR VERSION_MINOR VERSION_PATCH)

option(COMPRESSOR_WERROR "Treat warnings in our own targets as errors." OFF)
option(COMPRESSOR_ASAN "Build everything with AddressSanitizer." OFF)
option(COMPRESSOR_PROFILING "Make the PROFILE_* macros write result.json." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Global on purpose: googletest and benchmark must be instrumented too, or libc++'s
# container-overflow checks report false positives across the boundary.
# No -fsanitize=leak: Apple Clang rejects it on arm64, and on Linux ASan already includes LSan.
if(COMPRESSOR_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

add_subdirectory(dependencies/google/googletest)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
add_subdirectory(dependencies/google/benchmark)
# Headers only (dynamic_bitset). CONFIG mode because CMake 4 removed the FindBoost module (CMP0167).
find_package(Boost 1.81.0 REQUIRED CONFIG)
add_subdirectory(argparser)
add_subdirectory(compressor)
add_subdirectory(compressor_cli)
add_subdirectory(compressor_app)
add_subdirectory(compressor_benchmark)
add_subdirectory(compressor_example)
add_subdirectory(lib/instrumentor)

mark_as_advanced(
    BUILD_GMOCK BUILD_GTEST BUILD_SHARED_LIBS
    gmock_build_tests gtest_build_samples gtest_build_tests
    gtest_disable_pthreads gtest_force_shared_crt gtest_hide_internal_symbols
)

set_target_properties(gtest PROPERTIES FOLDER "dependencies")
set_target_properties(gtest_main PROPERTIES FOLDER "dependencies")
set_target_properties(gmock PROPERTIES FOLDER "dependencies")
set_target_properties(gmock_main PROPERTIES FOLDER "dependencies")
set_target_properties(benchmark PROPERTIES FOLDER "dependencies")
set_target_properties(benchmark_main PROPERTIES FOLDER "dependencies")
