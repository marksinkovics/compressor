# Up to 3.31 for CMP0156 (3.29): de-duplicated static libraries, so Apple ld doesn't warn
# about gtest and benchmark appearing twice on a link line.
cmake_minimum_required(VERSION 3.25...3.31)

# Before project(): that's where the toolchain file is read. A toolchain given on the command
# line wins; chain another one through VCPKG_CHAINLOAD_TOOLCHAIN_FILE.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{VCPKG_ROOT})
        message(FATAL_ERROR "Set VCPKG_ROOT to a vcpkg checkout; dependencies come from vcpkg.json.")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain")
endif()

enable_testing()

project(CompressionProject VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# -std=c++23, not gnu++23, for the cxx_std_23 our libraries require.
set(CMAKE_CXX_EXTENSIONS OFF)

option(COMPRESSOR_ASAN "Build our code with AddressSanitizer (needs an -asan triplet)." OFF)
option(COMPRESSOR_PROFILING "Make the PROFILE_* macros write result.json." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VcpkgPins)
compressor_check_vcpkg_pins()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Global on purpose: every target in this tree is ours; dependencies come prebuilt from vcpkg.
# CMAKE_COMPILE_WARNING_AS_ERROR adds -Werror or /WX.
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Clang 23 adds this to -Wextra; leaving out designated fields to default them is intended.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-Wmissing-designated-field-initializers
                            COMPRESSOR_HAS_DESIGNATED_INITIALIZER_WARNING)
    if(COMPRESSOR_HAS_DESIGNATED_INITIALIZER_WARNING)
        add_compile_options(-Wno-missing-designated-field-initializers)
    endif()
endif()

# No -fsanitize=leak: Apple Clang rejects it on arm64, and on Linux ASan already includes LSan.
if(COMPRESSOR_ASAN)
    if(NOT VCPKG_TARGET_TRIPLET MATCHES "-asan$")
        message(FATAL_ERROR "COMPRESSOR_ASAN needs an ASan triplet from triplets/, such as "
                            "-DVCPKG_TARGET_TRIPLET=arm64-osx-asan (now: '${VCPKG_TARGET_TRIPLET}').")
    endif()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_subdirectory(compressor)
add_subdirectory(compressor_cli)
add_subdirectory(compressor_app)
add_subdirectory(compressor_benchmark)
add_subdirectory(compressor_example)
add_subdirectory(lib/instrumentor)
