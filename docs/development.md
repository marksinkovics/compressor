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

The round-trip tests (`RoundTripTests`) encode and decode every file in `compressor_example/samples/raw/` and compare the result byte for byte; a file added there is tested automatically.

Tests for known bugs are added with a `DISABLED_` prefix until the fix lands. Run one with `--gtest_also_run_disabled_tests --gtest_filter=<Suite>.DISABLED_<name>`; some of them crash or hang before their fix, so run them one at a time.

## Code quality

Formatting and linting run through `tools/quality.sh`, with one stage per tool: `clang-format`, `clang-tidy`, `shellcheck` and `shfmt`. A pre-commit hook runs each stage on every commit.

### Setup

```bash
brew install pre-commit llvm shellcheck shfmt
pre-commit install
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

- clang-format and clang-tidy both come from Homebrew's `llvm`, which is keg-only (not on `PATH`). `quality.sh` calls them by full path (`$(brew --prefix llvm)/bin/...`), so another clang-format on `PATH` doesn't matter. Without Homebrew (Linux, CI) both come from `PATH`; PyPI has matching `clang-format` and `clang-tidy` wheels.
- clang-format must be major version 23 (`CLANG_FORMAT_MAJOR` in `quality.sh`; 23.1.3 is tested). Every major version formats a little differently, so `quality.sh` stops if it finds another one. When `brew upgrade llvm` moves to a new major version, bump `CLANG_FORMAT_MAJOR`, run `tools/quality.sh format`, commit the result on its own, and add that commit to `.git-blame-ignore-revs`.
- clang-tidy reads `build/debug/compile_commands.json`, so run `./generate.sh` first.
- `.git-blame-ignore-revs` lists the commit that reformatted the whole tree. The `git config` line is needed once per clone, so that `git blame` skips that commit.

### `tools/quality.sh`

```bash
tools/quality.sh check                        # every stage on the whole tree
tools/quality.sh check clang-tidy shfmt       # only the named stages
tools/quality.sh check clang-tidy -- a.h b.cpp  # clang-tidy only on the sources these files affect
tools/quality.sh --fix                        # format, apply clang-tidy fixes, then check every stage
tools/quality.sh format                       # format every C++ and shell file
tools/quality.sh format -- a.cpp b.sh         # format only the given files
tools/quality.sh stages                       # list the stage names
```

- Files are the tracked and new, not ignored files (`git ls-files`), so `build/` is skipped. C++ means `*.cpp *.h *.hpp`; shell means `*.sh`.
- With `-- file...`, clang-tidy checks only the sources that include those files, using `ninja -t deps` (changing `bitset.h` selects 19 of 41 sources). A full run takes about 30 s.
- Settings live in `.clang-format`, `.clang-tidy` (plus nested ones in `argparser/` and `compressor_benchmark/`), `.shellcheckrc` and `.editorconfig` (shfmt reads its options from there).

### pre-commit hooks

`.pre-commit-config.yaml` runs the generic `pre-commit-hooks` checks (whitespace, final newline, line endings, YAML/JSON, merge conflicts, large files, private keys) and one local hook per `quality.sh` stage. Each hook runs only when the commit touches its files; clang-tidy checks the sources the staged files affect, the other stages the whole tree.

```bash
pre-commit run --all-files    # every hook on the whole tree
pre-commit run clang-tidy --files compressor/include/compressor/bitset/bitset.h
```

### Claude Code format hook

`.claude/settings.json` registers `.claude/hooks/format-edited-file.sh` as a `PostToolUse` hook on `Edit` and `Write`. After Claude edits a C++ or shell file in the repo, the hook runs `tools/quality.sh format -- <file>`, so formatting never turns up as a lint failure. It always exits 0, so it never blocks an edit.

## Run benchmark

Tool: [Google benchmark framework](https://github.com/google/benchmark) (master branch)

:warning: Always use release configuration for benchmarking

After the project is compiled successfully, the benchmarking app is under _build/debug/compressor_benchmark_ folder where `compressor_benchmark` runs the predefined tests
