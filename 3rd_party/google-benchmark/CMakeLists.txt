# 
# Google Benchmark
# 

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing benchmark's tests" FORCE)

configure_file(CMakeLists.txt.in googlebenchmark-download/CMakeLists.txt)
# Call CMake to download and Google Test
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark-download )
if(result)
  message(FATAL_ERROR "CMake step for googlebenchmark failed: ${result}")
endif()
# Build the downloaded google benchmark
execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark-download )
if(result)
  message(FATAL_ERROR "Build step for googlebenchmark failed: ${result}")
endif()

# Add googlebenchmark directly to our build. This defines
# the benchmark and benchmark_main targets.
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark-src
                 ${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark-build)

# Add aliases for GBenchmark
if(NOT TARGET GBenchmark::GBenchmark)
    add_library(GBenchmark::GBenchmark ALIAS benchmark)
    add_library(GBenchmark::Main ALIAS benchmark_main)
endif()

