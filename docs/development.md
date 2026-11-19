# Development

The project uses [CMake](https://cmake.org) for managing dependencies, configurations and building. CMake 3.25 or later is required. In addition, [ninja build system](https://ninja-build.org) is used for development.

## Prerequisite

Last checked with CMake 4.4.5, Apple Clang 17 (Xcode 26.2) and vcpkg 2025-12-16 on macOS (2026-11-09).

* CMake 3.25 or later
* A C++23 compiler (Apple Clang 17 / Xcode 26 or later)
* Ninja
* [vcpkg](https://learn.microsoft.com/vcpkg/) cloned and bootstrapped, with `VCPKG_ROOT` pointing at it
* `build/debug/compile_commands.json` is generated for clang-tidy and editors

## Compile

Third-party libraries come only from vcpkg in manifest mode: `vcpkg.json` lists them (googletest, google benchmark, CLI11, zlib as the benchmark's reference codec) and pins the port versions through `builtin-baseline`. The root `CMakeLists.txt` uses `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` as the toolchain unless one is given, and configure installs the ports into `<build dir>/vcpkg_installed`; vcpkg's binary cache makes later configures fast. Every installed port, transitive and host ones included, is pinned to its exact version in `overrides`, and configure fails if one isn't (`cmake/VcpkgPins.cmake`). To add or update a dependency:

1. Change `dependencies` (and `builtin-baseline` to a newer vcpkg commit when updating).
2. Configure; the error lists every port without a matching override, with the version vcpkg chose.
3. Add or change those `overrides` entries; the `port-version` comes from `$VCPKG_ROOT/versions/<letter>-/<port>.json` or the port's `vcpkg.json`.

### Presets

`CMakePresets.json` has one configure, build, test and workflow preset per configuration. Each builds in `build/<preset>` with warnings as errors.

| Preset | Generator | Configuration |
| --- | --- | --- |
| `debug` | Ninja | Debug; its `compile_commands.json` feeds clang-tidy and editors |
| `release` | Ninja | Release, for benchmarks |
| `asan` | Ninja | Debug with `COMPRESSOR_ASAN=ON` and the `arm64-osx-asan` triplet (macOS only) |
| `xcode` | Xcode | Debug for the build and test presets (macOS only) |

```bash
cmake --workflow --preset debug   # configure, build and test in one go
cmake --preset debug              # configure only
cmake --build --preset debug      # build
ctest --preset debug              # test
```

Local variations (another generator, extra cache variables) go into `CMakeUserPresets.json`, which is ignored by git and can inherit from these presets.

### Build options

| Option | Default | Effect |
| --- | --- | --- |
| `CMAKE_COMPILE_WARNING_AS_ERROR` | `OFF` | CMake's own option: warnings are errors (`-Werror`, `/WX` on MSVC). The presets turn it on; configuring with `--compile-no-warning-as-error` overrides it. |
| `COMPRESSOR_ASAN` | `OFF` | AddressSanitizer for our code. Needs an ASan triplet from `triplets/` (`-DVCPKG_TARGET_TRIPLET=arm64-osx-asan` or `x64-linux-asan`), so gtest and benchmark are instrumented too; configure fails without one. Use with `CMAKE_BUILD_TYPE=Debug`. On Linux it also finds leaks; Apple Clang has no LeakSanitizer on arm64. |
| `COMPRESSOR_PROFILING` | `OFF` | The `PROFILE_*` macros write `result.json` (Chrome trace format) to the working directory. |

Every target gets `-Wall -Wextra -Wpedantic` (`/W4` on MSVC) from the root `add_compile_options`; every target built in this tree is ours, and vcpkg headers are included as system headers. Optimization comes from the build type only (Release: `-O3 -DNDEBUG`). Tests are discovered when `ctest` runs (`DISCOVERY_MODE PRE_TEST`), not at build time.

## Run tests

Tools

* [Google test framework](https://github.com/google/googletest)
* [CTest](https://cmake.org/cmake/help/latest/module/CTest.html)

Run `cmake --workflow --preset debug`, or `ctest --preset debug` after a build. A single binary runs directly too, for example `build/debug/compressor/test/compressor_test_app --gtest_filter='EngineTests.*'`.

The round-trip tests (`RoundTripTests`) encode and decode every file in `compressor_example/samples/raw/` and compare the result byte for byte; a file added there is tested automatically.

`PipeRoundTrip.*` (in `compressor_app/CMakeLists.txt`) run the built `compressor_app` through `compressor_app/test/pipe_roundtrip.cmake`: stdin-to-stdout encoding must match file encoding, and `encode | decode` must give the input back. They cover an empty input, `lorem_50p.txt` and `flower.png`.

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
- clang-tidy reads `build/debug/compile_commands.json`, so run `cmake --preset debug` first.
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
- With `-- file...`, clang-tidy checks only the sources that include those files, using `ninja -t deps`. A full run takes about 30 s.
- Settings live in `.clang-format`, `.clang-tidy` (plus a nested one in `compressor_benchmark/`), `.shellcheckrc` and `.editorconfig` (shfmt reads its options from there).

### pre-commit hooks

`.pre-commit-config.yaml` runs the generic `pre-commit-hooks` checks (whitespace, final newline, line endings, YAML/JSON, merge conflicts, large files, private keys) and one local hook per `quality.sh` stage. Each hook runs only when the commit touches its files; clang-tidy checks the sources the staged files affect, the other stages the whole tree.

```bash
pre-commit run --all-files    # every hook on the whole tree
pre-commit run clang-tidy --files compressor/include/compressor/huffman/huffman.h
```

### Claude Code format hook

`.claude/settings.json` registers `.claude/hooks/format-edited-file.sh` as a `PostToolUse` hook on `Edit` and `Write`. After Claude edits a C++ or shell file in the repo, the hook runs `tools/quality.sh format -- <file>`, so formatting never turns up as a lint failure. It always exits 0, so it never blocks an edit.

## Run benchmark

Tool: [Google benchmark framework](https://github.com/google/benchmark) (vcpkg port). Always benchmark the `release` preset.

```bash
bash tools/fetch-corpus.sh               # once: Canterbury and Silesia into corpus/ (git-ignored)
bash tools/bench.sh --out new.json       # all three sets, about 2 minutes (default: build/release/benchmark.json)
bash tools/bench.sh --filter 'Engine/.*/canterbury'
python3 tools/bench-compare.py docs/benchmarks/decode-table.json new.json
bash tools/bench-cli.sh --out cli.json  # compressor_app end to end, file I/O included
```

`tools/bench-cli.sh` times `compressor_app encode`/`decode` on `kennedy.xls`, `webster` and `mozilla` (or the files given, relative to the repo root), after checking that each round-trips. It uses [hyperfine](https://github.com/sharkdp/hyperfine) 2.x (`brew install hyperfine`) and falls back to a python3 loop without it; both write `results[].summary.time_wall_clock`, and the script prints a table from it.

How the benchmarks work, what the counters mean and the baseline numbers are in [benchmarks.md](benchmarks.md).
