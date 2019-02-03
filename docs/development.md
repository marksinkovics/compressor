# Development

The project uses [CMake](https://cmake.org) for managing dependencies, configurations and building. Currenly v3.10 is required for this project. In addition, [ninja build system](https://ninja-build.org) is used for development.

### Prerequisite

* CMake v3,10
* Clang (Apple LLVM version 10.0.1 (clang-1001.0.46.4))
* Xcode v10.2.1
* C++ (c++1z)
* Boost v1.68.0 has to be installed and make it visible for CMake
    * boost::dynamic_bitset, as a header only class, is used to benchmarking bit storing

## Compile

### Debug build

1. Create a build folder (move into the project)
    `mkdir -p build/debug && cd build/debug`
2. Generate the project (with Ninja)
   `cmake ../.. -G Ninja`
3.  Build the project (all targets)
    `ninja`
4. Binary files can be found under _build/debug_

### Release build

1. Create a build folder (move into the project)
    `mkdir -p build/release && cd build/release`
2. Generate the project (with Ninja)
   `cmake -DCMAKE_BUILD_TYPE=Release ../.. -G Ninja`
3.  Build the project (all targets)
    `ninja`
4. Binary files can be found under _build/debug_

Of course CMake has various options for project generation that can be used under _-G_ such as:

* Xcode
* Unix Makefiles
* Ninja
* etc.

## Run tests

Tool: [Google test framework](https://github.com/google/googletest) v1.8.0

After the project is compiled sucessfully, the unit test can be found under _build/debug/compressor_test_ folder where `compressor_test` run tests

## Run benchmark

Tool: [Google benchmark framework](https://github.com/google/benchmark) (master branch)

:warning: Always use release configuration for benchmarking

After the project is compiled sucessfully, the benchmarking app is under _build/debug/compressor_benchmark_ folder where `compressor_benchmark` runs the predefined tests
