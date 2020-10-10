cmake_minimum_required(VERSION 3.10)
enable_testing()

message(STATUS "Current generator: ${CMAKE_GENERATOR}")

if (CMAKE_GENERATOR MATCHES "Xcode")
    set(CMAKE_OSX_ARCHITECTURES "x86_64")
endif()

project(CompressionProject CXX)

set(VERSION_MAJOR 0 CACHE STRING "Project major version number.")
set(VERSION_MINOR 0 CACHE STRING "Project minor version number.")
set(VERSION_PATCH 1 CACHE STRING "Project patch version number.")
mark_as_advanced(VERSION_MAJOR VERSION_MINOR VERSION_PATCH)

set (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#
# Enable profiling globally
#
add_compile_definitions(PROFILING=0)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z -Wall -Wextra -Werror")

if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1")
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
endif()

add_subdirectory(dependencies/google/googletest)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
add_subdirectory(dependencies/google/benchmark)
add_subdirectory(dependencies/boostorg/boost)
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

if(NOT ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()
