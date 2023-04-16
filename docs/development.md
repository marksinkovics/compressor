# Development

The project uses [CMake](https://cmake.org) for managing dependencies, configurations and building. Currently v3.10 is required for this project. In addition, [ninja build system](https://ninja-build.org) is used for development.

## Prerequisite

* CMake v3.25.1
* Clang (Apple LLVM version 14.0.0 (clang-1400.0.29.202))
* Xcode v 14.2 (14C18)
* C++ (c++1z)
* Ninja
* Boost v1.81.0 has to be installed and make it visible for CMake
  * boost::dynamic_bitset, as a header only class, is used to benchmarking bit storing

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
