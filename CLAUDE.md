# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Huffman-coding compressor in C++ (CMake + Ninja), macOS first, then Linux and Windows. Being modernized in phases; the plan, decisions and known issues are in `docs/roadmap.md`.

## Status (keep current)

- Phase 0 (pre-commit hooks and code quality) is done: `tools/quality.sh`, pre-commit hooks installed, Claude format hook in `.claude/settings.json`.
- Phase 1 (safety-net tests) is done: round-trip tests over `compressor_example/samples/raw/` and 7 `DISABLED_` edge-case tests for the Phase 2 bugs.
- Phase 2 (bug fixes) is done: all edge-case tests enabled, 91/91 pass; `compressor_app` returns non-zero on failure.
- Phase 3 (build, tooling and platforms) in progress: steps "CMake" (CMake 3.25+, C++23), "Flags per target" (`COMPRESSOR_*` options, native arm64 Xcode) and "vcpkg" (manifest, global warning flags, ASan triplets), "Presets" and "Quality tools on the new build" are done; step "CI" is deferred.
- Phase 4 (CLI11) is done: `argparser/` deleted, `encode`/`decode` subcommands with `-i`/`-o`, `--help`, `--version`; 65/65 pass.
- The `asan` workflow passes 65/65 when run by hand (it can't run in Claude's sandbox).
- Phase 5 (benchmarks and performance) in progress: steps "Benchmark harness and baseline" (`docs/benchmarks.md`, `docs/benchmarks/baseline.json`), "Canonical Huffman with length-limited codes" (`compressor/huffman/`, format in `docs/file_format.md`) and "Decode lookup table" (15-bit cap, decode 170–340 MiB/s; `docs/benchmarks/decode-table.json`) are done; 86/86 pass. The freestanding core is postponed to Phase 6.
- **Next:** Phase 5 step "Encode bit buffer" (`docs/roadmap.md`); start with a plan for approval.

## Commands

```bash
cmake --workflow --preset debug   # configure, build and test build/debug (also: release, asan, xcode)
cmake --build --preset debug
ctest --preset debug -R BitsetTests
./build/debug/compressor/test/compressor_test_app --gtest_filter='BitsetTests.*'
bash tools/quality.sh check       # clang-format, clang-tidy, shellcheck, shfmt (see docs/development.md)
bash tools/bench.sh               # release benchmarks -> JSON; corpora via tools/fetch-corpus.sh (docs/benchmarks.md)
```

- Needs `VCPKG_ROOT` set to a vcpkg checkout; the root `CMakeLists.txt` takes vcpkg's toolchain from there, and configure installs `vcpkg.json` (gtest, benchmark, boost-dynamic-bitset, cli11). Third-party code comes only through vcpkg. Every port, transitive and host ones included, needs an exact `overrides` pin in `vcpkg.json`; configure fails otherwise.
- Options: `COMPRESSOR_ASAN` (the `asan` preset; needs an `-asan` triplet from `triplets/`), `COMPRESSOR_PROFILING` (`PROFILE_*` macros write `result.json`). Presets set `CMAKE_COMPILE_WARNING_AS_ERROR`; warning flags are global (`docs/development.md`).

## Architecture

`compressor_app` → `compressor_cli` (CLI11) → `compressor` → `lib/instrumentor`.

- `compressor/`: `Compressor` runs task.reader() → task.engine() → task.writer(). `huffman/` computes length-limited code lengths (package-merge) and canonical codes; `Engine` counts frequencies, encodes through per-symbol `bitset` codes, decodes through a `DecodeTable` indexed by the next 15 bits, and rebuilds a `tree/` only for `print_graph`. The file format (`docs/file_format.md`) is the `operator<<`/`operator>>` on `EncodedData` in `data/data.cpp`.
- Every component sits behind an interface (`ICompressor`, `IEngine`, `IEncoderTask`, `IDataReader<T>`, …) with a gmock mock in `<pkg>/test/include/<pkg>/test/mock/`.
- `cmake/PackageTest.cmake`'s `package_add_test` builds each test package twice: as a library, so other test packages can reuse its mocks, and as `<name>_app`, the runnable gtest binary.
- Source files are listed explicitly in each `CMakeLists.txt`; add new files there.

## Workflow

- The conversation is cleared between steps, so this Status section and `docs/roadmap.md` are the handover. At the end of every step, run the `finish-step` skill without being asked.
- Every fix or feature lands with a test.
- Get the code lint-clean (`bash tools/quality.sh check`) before running the tests.
