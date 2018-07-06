cmake_minimum_required(VERSION 3.10)

project(CompressionProject)

enable_testing()

set (CMAKE_CXX_STANDARD 14)

add_subdirectory(compressor)
add_subdirectory(compressor_app)
add_subdirectory(3rd_party/google-test)
add_subdirectory(compressor_test)
