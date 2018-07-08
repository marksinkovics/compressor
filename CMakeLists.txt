cmake_minimum_required(VERSION 3.10)

project(CompressionProject)

enable_testing()

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(compressor)
add_subdirectory(compressor_app)
add_subdirectory(3rd_party/google-test)
add_subdirectory(compressor_test)
add_subdirectory(3rd_party/google-benchmark)
add_subdirectory(compressor_benchmark)

