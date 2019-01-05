cmake_minimum_required(VERSION 3.10)

project(CompressionProject)

enable_testing()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_FLAGS "-std=c++1z -Wall -Wextra -Werror")

if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
endif()

add_subdirectory(3rd_party/boost)
add_subdirectory(compressor)
add_subdirectory(compressor_app)
add_subdirectory(3rd_party/google-test)
add_subdirectory(compressor_test)
add_subdirectory(3rd_party/google-benchmark)
add_subdirectory(compressor_benchmark)

if(NOT ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()