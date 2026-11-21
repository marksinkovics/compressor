# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Huffman-coding compressor in C++ (CMake + Ninja), macOS first, then Linux and Windows. Being modernized in phases; the plan, decisions and known issues are in `docs/roadmap.md`.

## Status (keep current)

- Phase 0 (pre-commit hooks and code quality) is done: `tools/quality.sh`, pre-commit hooks installed, Claude format hook in `.claude/settings.json`.
- Phase 1 (safety-net tests) is done: round-trip tests over `compressor_example/samples/raw/` and 7 `DISABLED_` edge-case tests for the Phase 2 bugs.
- Phase 2 (bug fixes) is done: all edge-case tests enabled, 91/91 pass; `ingot` returns non-zero on failure.
- Phase 3 (build, tooling and platforms) in progress: steps "CMake" (CMake 3.25+, C++23), "Flags per target" (`COMPRESSOR_*` options, native arm64 Xcode) and "vcpkg" (manifest, global warning flags, ASan triplets), "Presets" and "Quality tools on the new build" are done; step "CI" is deferred.
- Phase 4 (CLI11) is done: `argparser/` deleted, `encode`/`decode` subcommands with `-i`/`-o`, `--help`, `--version`; 65/65 pass.
- The `asan` workflow passes 65/65 when run by hand (it can't run in Claude's sandbox).
- Phase 5 (benchmarks and performance) is done: canonical length-limited Huffman (`compressor/huffman/`), decode table, 64-bit bit writer, block stream I/O, corrupt input as `compressor::Error`, and a stored fallback behind a method byte (format in `docs/file_format.md`, numbers in `docs/benchmarks.md`); 94/94 pass. The freestanding core is postponed to Phase 6.
- Container format (renaming the project to **ingot**: magic `\x89ING`, `.ingot`): step 1 (frame header, 1 MiB blocks with per-block method and CRC32, end block; `docs/file_format.md`) step 2 (block streaming through `BlockEncoder`/`BlockDecoder`, output via temp file and rename) and step 3 (`-`/omitted `-i`/`-o` for stdin/stdout through `stream/stdio.h`) are done; 142/142 pass.
- Hardware CRC32 is done: `crc32()` uses ARMv8 CRC32 or x86 PCLMULQDQ, chosen by a one-time CPU check (no build flags needed), else `crc32_portable()`; 144/144 pass. Checked with GCC on arm64 and x86-64 Linux; MSVC is not verified yet.
- Multithreaded blocks are done: `thread/pool.h`'s `ThreadPool`, one engine per worker, `-T/--threads` (default all cores); 166/166 pass, clean under an ad hoc TSan build.
- Folder archives step 1 is done: solid blocks plus an index after the end block, flag bit 0 in the frame header; `encode -i <dir>` archives, `decode` extracts into a new directory (`compressor/archive/`, format in `docs/file_format.md`); 202/202 pass.
- Folder archives step 2 is done: `list [-l]`, `extract ARCHIVE PATH [-o OUT]`, `cat`, `verify`; extraction decodes only the blocks a selection needs (`IBlockDecoder::decode_blocks`); 236/236 pass.
- The autonomous run (branch `feat/autonomous-run`, list and results at the end of `docs/roadmap.md`) is done, 337/337 pass:
  - archives: `stats`, deduplication, `grep` (only the given paths' blocks), `search` through an opt-in FM-index (exact but slower than `grep`), file I/O through stdio (folder extraction 54× faster);
  - formats: `encode -f gzip|zip` with our own Deflate; ingot format version 2 with pipeline blocks: per-block transforms (delta, ARM64/x86 branch filters) and coders LZ77 + Huffman (inflate is ours), BWT + rANS, order-0 rANS, LZR (LZMA-style); `encode -L 1..9`, default 6;
  - quality: libFuzzer targets and the `fuzz` preset, ARM CRC32 at 23.5 GB/s, clang 23 clean.
- `decode`, `verify` and `grep` read `.gz` files with our own streaming inflater; 343/343 pass.
- The search index is searched in place (wavelet matrix through `mmap`): rare patterns 12–15× faster than `grep`, common ones as fast; 345/345 pass.
- Zip64 is done: zip archives with files over 4 GiB or more than 65 535 entries; 347/347 pass.
- Autonomous run 2 (list at the end of `docs/roadmap.md`, same branch, Claude commits): item 1, multithreaded Deflate (`-f gzip`/`-f zip` take `-T` and `-L`), item 2, the CM coder (coder 6 at level 9, 11.4% below level 8 on Silesia), and item 4, the 2-D median image predictor (transform 5), are done; item 3 (`grep` prefilter) was skipped as not worth it; 360/360 pass. Run 2 is finished.
- Autonomous run 3 (list at the end of `docs/roadmap.md`, same branch, Claude commits): item 1, the rename (the program is `ingot`, in `ingot/`; libraries keep `compressor`), item 2, `tools/compare.py` against gzip, zstd, xz, bzip2, brotli with history in `docs/benchmarks/history/`, item 3, BWT + CM (coder 7, level 9) and tANS (coder 8), item 4, dictionaries (`ingot train`, `-D`, coders 9 and 10), item 5, the `ingot` Python module (`python/`, preset `python`), and item 6, the WebAssembly demo (`web/`, `tools/web.sh`, emsdk in `build/emsdk`), are done; 382/382 pass (383 with `python`). Run 3 is finished.
- Autonomous run 4 (list at the end of `docs/roadmap.md`, same branch, Claude commits): item 1, dictionary follow-ups (CM primed, coder 11; codes in the dictionary, coder 12; single-block frames for `-D`; `stats -D`), and item 2, Python follow-ups (a wheel per build, `ingot.Compressor`/`Decompressor`, `list`/`read`/`extract`), and item 3, coders (LZT, coder 13: LZ77 + tANS at level 7; PPM measured and removed), are done; 394/394 pass (396 with `python`). Run 4 is finished.
- Autonomous run 5 (list at the end of `docs/roadmap.md`, same branch, Claude commits; speed measured as CPU time): item 1, level 6 encoding 2.8× faster per core (masked chain table, multiplicative hash, 4-byte chains via `block_options()`), and item 2, coder 14 (LZT with an optimal parse and three repeat offsets) at level 7 (Silesia −6.3%), are done; 399/399 pass. Run 5 is finished.
- Autonomous run 6 (list at the end of `docs/roadmap.md`, same branch, Claude commits): item 1, byte-range reads (`cat --range`, `range/`), and item 2, archive order (files grouped by extension), are done; 408/408 pass.
- **Next:** run 6 item 3, the FSKit mount spike (local signing first; postpone if blocked, no NFS).

## Commands

```bash
cmake --workflow --preset debug   # configure, build and test build/debug (also: release, asan, xcode)
cmake --build --preset debug
ctest --preset debug -R EngineTests
./build/debug/compressor/test/compressor_test_app --gtest_filter='EngineTests.*'
cmake --workflow --preset fuzz    # libFuzzer + ASan build, unit tests and short fuzz runs (docs/development.md, "Fuzzing")
cmake --workflow --preset python  # also the ingot Python module (docs/development.md, "Python module")
bash tools/quality.sh check       # clang-format, clang-tidy, shellcheck, shfmt (see docs/development.md)
bash tools/bench.sh               # release benchmarks -> JSON; corpora via tools/fetch-corpus.sh (docs/benchmarks.md)
bash tools/bench-cli.sh           # ingot end to end with hyperfine (python3 fallback)
python3 tools/compare.py          # against gzip/zstd/xz/bzip2/brotli; history in docs/benchmarks/history/
bash tools/web.sh serve           # WebAssembly demo on localhost:8000 (needs build/emsdk; docs/development.md)
```

- Needs `VCPKG_ROOT` set to a vcpkg checkout; the root `CMakeLists.txt` takes vcpkg's toolchain from there, and configure installs `vcpkg.json` (gtest, benchmark, cli11, zlib). Third-party code comes only through vcpkg. Every port, transitive and host ones included, needs an exact `overrides` pin in `vcpkg.json`; configure fails otherwise.
- Options: `COMPRESSOR_ASAN` (the `asan` preset; needs an `-asan` triplet from `triplets/`), `COMPRESSOR_PROFILING` (`PROFILE_*` macros write `result.json`). Presets set `CMAKE_COMPILE_WARNING_AS_ERROR`; warning flags are global (`docs/development.md`).

## Architecture

`ingot` → `compressor_cli` (CLI11) → `compressor` → `lib/instrumentor`.

- `compressor_cli`: `CLI` takes a `CompressorFactory` called after parsing with the `-T/--threads` value.

- `compressor/`: `Compressor` opens the files (or archives a directory input) (or stdin/stdout for `-`, through `stream/stdio.h`'s buffers) and runs `stream/`'s `BlockEncoder`/`BlockDecoder` over `std::istream&`/`std::ostream&`, writing a regular output through a temp file that it renames on success. The coders read and write the container (`docs/file_format.md`) one block at a time with `write_block`/`FrameReader` from `data/data.h`, and run `IEngine::encode_block`/`decode_block` (stored, Huffman, or a pipeline block: LZ77 + Huffman from level 2, BWT and LZR from 8, `cm/` context mixing and BWT + CM at 9, `lzt/` (LZ77 + tANS, optimal parse) at 7, order-0 rANS or tANS only after a `transform/`, which is used when it helps; with `-D` the LZ77 and LZR coders are primed by a `dictionary/`; per block, checked with `checksum/crc32`) on a `thread/pool.h` `ThreadPool`: one engine per worker from an `EngineFactory`, at most 2 blocks per thread in flight, written in block order. `archive/` walks a folder into `Entry`s and streams the files through `encode_frame` as one container, with an index after the end block; extraction, `extract`/`cat` and `verify` read the index first, decode only the blocks covering the selected files (`decode_blocks` over the block table) and split the data back into files. `huffman/` computes length-limited code lengths (package-merge) and canonical codes, for any alphabet size; `lz77/` finds Deflate-style matches and `deflate/` writes Deflate, gzip and zip (`encode_as`); `Engine` encodes through a 64-bit `BitWriter` into an MSB-first payload, decodes through a `DecodeTable` indexed by the next 15 bits, and rebuilds a `tree/` only for `print_graph`.
- Every component sits behind an interface (`ICompressor`, `IBlockEncoder`, `IBlockDecoder`, `IEngine`) with a gmock mock in `<pkg>/test/include/<pkg>/test/mock/`.
- `cmake/PackageTest.cmake`'s `package_add_test` builds each test package twice: as a library, so other test packages can reuse its mocks, and as `<name>_app`, the runnable gtest binary.
- Source files are listed explicitly in each `CMakeLists.txt`; add new files there.

## Workflow

- The conversation is cleared between steps, so this Status section and `docs/roadmap.md` are the handover. At the end of every step, run the `finish-step` skill without being asked.
- Every fix or feature lands with a test.
- Get the code lint-clean (`bash tools/quality.sh check`) before running the tests.
