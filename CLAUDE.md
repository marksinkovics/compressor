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
- Phase 5 (benchmarks and performance) is done: canonical length-limited Huffman (`compressor/huffman/`), decode table, 64-bit bit writer, block stream I/O, corrupt input as `compressor::Error`, and a stored fallback behind a method byte (format in `docs/file_format.md`, numbers in `docs/benchmarks.md`); 94/94 pass. The freestanding core is postponed to Phase 6.
- Container format (renaming the project to **ingot**: magic `\x89ING`, `.ingot`): step 1 (frame header, 1 MiB blocks with per-block method and CRC32, end block; `docs/file_format.md`) step 2 (block streaming through `BlockEncoder`/`BlockDecoder`, output via temp file and rename) and step 3 (`-`/omitted `-i`/`-o` for stdin/stdout through `stream/stdio.h`) are done; 142/142 pass.
- Hardware CRC32 is done: `crc32()` uses ARMv8 CRC32 or x86 PCLMULQDQ, chosen by a one-time CPU check (no build flags needed), else `crc32_portable()`; 144/144 pass. Checked with GCC on arm64 and x86-64 Linux; MSVC is not verified yet.
- Multithreaded blocks are done: `thread/pool.h`'s `ThreadPool`, one engine per worker, `-T/--threads` (default all cores); 166/166 pass, clean under an ad hoc TSan build.
- Folder archives step 1 is done: solid blocks plus an index after the end block, flag bit 0 in the frame header; `encode -i <dir>` archives, `decode` extracts into a new directory (`compressor/archive/`, format in `docs/file_format.md`); 202/202 pass.
- **Next:** "Folder compression" step 2 in `docs/roadmap.md`: `list`, `extract <path>`, `cat`, `verify`. The file-output speed known issue is a smaller option.

## Commands

```bash
cmake --workflow --preset debug   # configure, build and test build/debug (also: release, asan, xcode)
cmake --build --preset debug
ctest --preset debug -R EngineTests
./build/debug/compressor/test/compressor_test_app --gtest_filter='EngineTests.*'
bash tools/quality.sh check       # clang-format, clang-tidy, shellcheck, shfmt (see docs/development.md)
bash tools/bench.sh               # release benchmarks -> JSON; corpora via tools/fetch-corpus.sh (docs/benchmarks.md)
bash tools/bench-cli.sh           # compressor_app end to end with hyperfine (python3 fallback)
```

- Needs `VCPKG_ROOT` set to a vcpkg checkout; the root `CMakeLists.txt` takes vcpkg's toolchain from there, and configure installs `vcpkg.json` (gtest, benchmark, cli11, zlib). Third-party code comes only through vcpkg. Every port, transitive and host ones included, needs an exact `overrides` pin in `vcpkg.json`; configure fails otherwise.
- Options: `COMPRESSOR_ASAN` (the `asan` preset; needs an `-asan` triplet from `triplets/`), `COMPRESSOR_PROFILING` (`PROFILE_*` macros write `result.json`). Presets set `CMAKE_COMPILE_WARNING_AS_ERROR`; warning flags are global (`docs/development.md`).

## Architecture

`compressor_app` → `compressor_cli` (CLI11) → `compressor` → `lib/instrumentor`.

- `compressor_cli`: `CLI` takes a `CompressorFactory` called after parsing with the `-T/--threads` value.

- `compressor/`: `Compressor` opens the files (or archives a directory input) (or stdin/stdout for `-`, through `stream/stdio.h`'s buffers) and runs `stream/`'s `BlockEncoder`/`BlockDecoder` over `std::istream&`/`std::ostream&`, writing a regular output through a temp file that it renames on success. The coders read and write the container (`docs/file_format.md`) one block at a time with `write_block`/`FrameReader` from `data/data.h`, and run `IEngine::encode_block`/`decode_block` (stored or Huffman per block, checked with `checksum/crc32`) on a `thread/pool.h` `ThreadPool`: one engine per worker from an `EngineFactory`, at most 2 blocks per thread in flight, written in block order. `archive/` walks a folder into `Entry`s and streams the files through `encode_frame` as one container, with an index after the end block; extraction reads the index first and splits the decoded data back into files. `huffman/` computes length-limited code lengths (package-merge) and canonical codes; `Engine` encodes through a 64-bit `BitWriter` into an MSB-first payload, decodes through a `DecodeTable` indexed by the next 15 bits, and rebuilds a `tree/` only for `print_graph`.
- Every component sits behind an interface (`ICompressor`, `IBlockEncoder`, `IBlockDecoder`, `IEngine`) with a gmock mock in `<pkg>/test/include/<pkg>/test/mock/`.
- `cmake/PackageTest.cmake`'s `package_add_test` builds each test package twice: as a library, so other test packages can reuse its mocks, and as `<name>_app`, the runnable gtest binary.
- Source files are listed explicitly in each `CMakeLists.txt`; add new files there.

## Workflow

- The conversation is cleared between steps, so this Status section and `docs/roadmap.md` are the handover. At the end of every step, run the `finish-step` skill without being asked.
- Every fix or feature lands with a test.
- Get the code lint-clean (`bash tools/quality.sh check`) before running the tests.
