# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Huffman-coding compressor in C++ (CMake + Ninja), macOS first, then Linux and Windows. Being modernized in phases; the plan, decisions and known issues are in `docs/roadmap.md`.

## Status (keep current)

- Phase 0 (pre-commit hooks and code quality) is done: `tools/quality.sh`, pre-commit hooks installed, Claude format hook in `.claude/settings.json`.
- **Next:** Phase 1, safety-net tests: the round-trip test over `compressor_example/samples/raw/` (`docs/roadmap.md`).

## Commands

Until Phase 3 replaces them with CMake presets and vcpkg:

```bash
./generate.sh                 # configure build/debug, build/release (Ninja), build/xcode
./build.sh                    # cmake --build build/debug
ctest --test-dir build/debug -R BitsetTests
./build/debug/compressor/test/compressor_test_app --gtest_filter='BitsetTests.*'
bash tools/quality.sh check   # clang-format, clang-tidy, shellcheck, shfmt (see docs/development.md)
```

- Needs Boost 1.81+ installed (`brew install boost`; headers only, found in CONFIG mode). googletest and benchmark are downloaded during configure.
- `-Werror` is on globally. `-DENABLE_ASAN=ON` for ASan/LSan. Set `PROFILING=1` in the root `CMakeLists.txt` to make the `PROFILE_*` macros write `result.json`.

## Architecture

`compressor_app` → `compressor_cli` → `compressor` + `argparser` → `lib/instrumentor`.

- `compressor/`: `Compressor` runs task.reader() → task.engine() → task.writer(). `Engine` holds the Huffman logic (frequency count, tree build, codes as `bitset`, bit-by-bit decode through `bit_dict_`). The file format is the `operator<<`/`operator>>` on `EncodedData` in `data/data.cpp`.
- Every component sits behind an interface (`ICompressor`, `IEngine`, `IEncoderTask`, `IDataReader<T>`, …) with a gmock mock in `<pkg>/test/include/<pkg>/test/mock/`.
- `cmake/PackageTest.cmake`'s `package_add_test` builds each test package twice: as a library, so other test packages can reuse its mocks, and as `<name>_app`, the runnable gtest binary.
- Source files are listed explicitly in each `CMakeLists.txt`; add new files there.

## Workflow

- The conversation is cleared between steps, so this Status section and `docs/roadmap.md` are the handover. At the end of every step, run the `finish-step` skill without being asked.
- Every fix or feature lands with a test.
- Get the code lint-clean (`bash tools/quality.sh check`) before running the tests.
