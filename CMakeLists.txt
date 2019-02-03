cmake_minimum_required(VERSION 3.10)

project(CompressionProject)

set(VERSION_MAJOR 0 CACHE STRING "Project major version number.")
set(VERSION_MINOR 0 CACHE STRING "Project minor version number.")
set(VERSION_PATCH 1 CACHE STRING "Project patch version number.")
mark_as_advanced(VERSION_MAJOR VERSION_MINOR VERSION_PATCH)

enable_testing()

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

add_subdirectory(3rd_party/boost)
add_subdirectory(3rd_party/google-test)
add_subdirectory(3rd_party/google-benchmark)

add_subdirectory(argparser)

add_subdirectory(compressor)

add_subdirectory(compressor_cli)

add_subdirectory(compressor_app)

add_subdirectory(compressor_benchmark)

add_subdirectory(tests)

if(NOT ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
    set(BUILD_DOCUMENTATION 1)
endif()

add_subdirectory(docs/doxygen)
