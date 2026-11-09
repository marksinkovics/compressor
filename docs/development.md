# Development

The project uses [CMake](https://cmake.org) for managing dependencies, configurations and building. CMake 3.25 or later is required. In addition, [ninja build system](https://ninja-build.org) is used for development.

## Prerequisite

Last checked with CMake 4.4.5, Apple Clang 17 (Xcode 26.2) and Boost 1.92 on macOS (2026-11-09).

* CMake 3.25 or later
* Clang (C++17, `-std=c++1z`)
* Ninja
* Boost 1.81 or later (`brew install boost`), found through its `BoostConfig.cmake`
  * `boost::dynamic_bitset`, header only, used by the bitset benchmark and the bitset tests
* `build/debug/compile_commands.json` is generated for clang-tidy and editors

## Compile

### Clone

This project manages 3rd party dependencies via CMake ExternalProject.

### Generate the project files for Ninja and Xcode

1. Go to the root folder of the project
2. Prepare folders (build/debug, build/release and build/xcode) an generate project files (Ninja, Xcode)
   Run `./configure.sh`
3. Run `./build.sh` to build in debug mode

### Debug build (with Ninja)

1. Generate the project (with Ninja)
   `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -G Ninja`
2. Build the project (all targets)
    `cmake --build build/debug` or `./build.sh`
3. Binary files can be found under _build/debug_

### Release build (with Ninja)

1. Generate the project (with Ninja)
   `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja`
2. Build the project (all targets)
    `cmake --build build/release`
3. Binary files can be found under _build/release_

Of course CMake has various options for project generation that can be used under _-G_ such as:

* Xcode
* Unix Makefiles
* Ninja
* etc.

## Run tests

Tools

* [Google test framework](https://github.com/google/googletest)
* [CTest](https://cmake.org/cmake/help/latest/module/CTest.html)

1. Generate the project (with Ninja)
   `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -G Ninja`
2. Build the project (all targets)
    `cmake --build build/debug` or `./build.sh`
3. Run unit tests with CTest
    `ctest`

## Run benchmark

Tool: [Google benchmark framework](https://github.com/google/benchmark) (master branch)

:warning: Always use release configuration for benchmarking

After the project is compiled successfully, the benchmarking app is under _build/debug/compressor_benchmark_ folder where `compressor_benchmark` runs the predefined tests
