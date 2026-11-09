# Roadmap

The phased plan for getting the basics right, the known issues it addresses, and feature ideas for afterwards.

## Decisions

- **Dependencies:** vcpkg in manifest mode (`vcpkg.json` with a pinned `builtin-baseline`), using the toolchain file from `$VCPKG_ROOT`.
- **Language:** C++23. Apple Clang 17 (Xcode 26) supports `<expected>`, `<print>` and `<span>`; checked 2026-11-08.
- **Argument parsing:** CLI11 replaces our own `argparser/`.
- **Platforms:** macOS first, then Linux and Windows.
- **File format:** breaking compatibility with existing compressed files is fine.

## Phased plan

Each phase is one or more steps, and every step should end with tests passing. When a step is done, mark it `(done)` and replace its plan text with what was actually built.

### Phase 0: Pre-commit hooks and code quality

Set up before any other coding, following FeatherOS (`tools/quality.sh` with one pre-commit hook per stage, a Claude format hook) and Runlight (the generic `pre-commit-hooks` set). Installed: `pre-commit`, `clang-format` 20.1.7, `shellcheck`, `shfmt`, and `clang-tidy` 23.1.3 from `brew install llvm` (`/opt/homebrew/opt/llvm/bin`, not on `PATH`; it also has `run-clang-tidy` and `clang-apply-replacements`).

- [x] **Build check (done):** CMake 4.4.5 and Apple Clang 17 configure and build Debug, Release and the Xcode project with `-Werror`; 74/74 tests pass. `cmake_minimum_required(VERSION 3.10)` is still accepted. Changes needed:
  - Boost: CMake 4 removed the `FindBoost` module (CMP0167), so the root `CMakeLists.txt` now calls `find_package(Boost 1.81.0 REQUIRED CONFIG)` and `compressor_benchmark` and `compressor_test` link `Boost::headers`. The `dependencies/boostorg/` wrapper and the unused `${Boost_INCLUDE_DIR}` on other targets are gone. Homebrew ships Boost 1.92, which works.
  - `CMAKE_EXPORT_COMPILE_COMMANDS ON` in the root `CMakeLists.txt`; `build/debug/compile_commands.json` exists after `./generate.sh`.
  - The googletest/benchmark download templates (`dependencies/google/*/CMakeLists.txt.in`) set `DOWNLOAD_EXTRACT_TIMESTAMP TRUE` (CMP0135) and require CMake 3.25 (CMP0114 warning under Xcode).
  - Left as is: googletest 1.13's own `cmake_minimum_required` deprecation warnings and `ld: warning: ignoring duplicate libraries` (gtest and benchmark linked twice via `package_add_test`). Both go away with vcpkg in Phase 3.
- [x] **Config files (done):** `run-clang-tidy -p build/debug` over the whole tree passes; Debug and Release build with `-Werror`; 74/74 tests pass.
  - `.editorconfig`: UTF-8, LF, final newline, trimmed trailing whitespace, 4-space indent; 2 spaces for YAML/JSON; Markdown keeps trailing whitespace; shfmt options for `.sh` (`binary_next_line`, `switch_case_indent`, `space_redirects`, as in FeatherOS).
  - `.clang-format`: FeatherOS's (LLVM base, 4 spaces, 100 columns, `PointerAlignment: Left`) with changes: Allman braces except lambda bodies (`BeforeLambdaBody: false`, `LambdaBodyIndentation: OuterScope`), leading-comma constructor initializers one per line (`BreakConstructorInitializers: BeforeComma`, `PackConstructorInitializers: Never`, as the code already did), and includes sorted within their existing blocks (`SortIncludes: CaseSensitive`, `IncludeBlocks: Preserve`). The tree still has about 750 formatting differences; the reformat step below fixes them (and must confirm the sorted includes still build).
  - `.clang-tidy`: `bugprone-*`, `clang-analyzer-*`, `performance-*`, `modernize-*`, `WarningsAsErrors: '*'`. Header filter is everything except `build/` (downloaded googletest/benchmark) and `argparser/`. Off for good: `bugprone-easily-swappable-parameters`, `modernize-use-nodiscard`, `modernize-use-trailing-return-type`. Off until Phase 5 (all findings sit in bitset, the tree and `Engine`; the comment in the file says to turn them back on): `bugprone-signed-bitwise`, `bugprone-narrowing-conversions`, `bugprone-exception-escape` (also fires on the two example `main`s, which will need a try/catch), `bugprone-derived-method-shadowing-base-method`.
  - Nested configs (`InheritParentConfig: true`): `argparser/.clang-tidy` turns off the eight checks that fire there, and Phase 4 deletes it with `argparser/`; `compressor_benchmark/.clang-tidy` turns off `clang-analyzer-deadcode.DeadStores` and `bugprone-throwing-static-initialization` for google benchmark's `for (auto _ : state)` and `BENCHMARK()` idioms. A nested config doesn't apply to its headers when another package's file includes them, hence the `argparser/` header exclusion in the root file.
  - Fixed: `override` instead of `virtual` on overrides, `= default`, pass-by-value plus `std::move` for sink parameters (task, node, tree and iterator constructors/setters), const-ref lambda parameters in `Engine`, `static_cast` for C-style casts, nested namespace `compressor::utils`, `std::make_unique`, `std::scoped_lock`, raw string literals in `Instrumentor`, `noexcept` on `bitset`'s move assignment. `compressor_example` no longer has throwing globals. In `bitset` tests: the moved-from check carries a `NOLINT`, `out4` is initialized, `in4` is written as `char`. `Engine::print_dict` casts the byte to `char` explicitly (same output).
  - `.shellcheckrc`: `external-sources=true`. `generate.sh` and `build.sh` pass shellcheck; shfmt wants one blank line removed in `generate.sh` (reformat step).
- [x] **`tools/quality.sh` (done)**, adapted from FeatherOS's. On the current tree `clang-tidy` and `shellcheck` pass; `clang-format` and `shfmt` fail until the reformat step. Tested on a scratch copy of the repo: a C-style cast fails `check clang-tidy`; `--fix` fixes it and then passes every stage; after `format`, all four stages pass, so the reformatted tree is clang-tidy clean as well.
  - Stages `clang-format`, `clang-tidy`, `shellcheck`, `shfmt`. Commands `check [stage...] [-- file...]`, `--fix` (format, then serial `clang-tidy --fix --warnings-as-errors='-*'`, then check every stage), `format`, `stages`. More than one stage prints `ok`/`FAIL` with timing per stage and a total; a single stage (one pre-commit hook) prints only diagnostics.
  - Files come from `git ls-files --cached --others --exclude-standard` (`*.cpp *.h *.hpp`, `*.sh`), so new files are covered and `build/` is skipped. shfmt takes its options from `.editorconfig` (no flags on the command line).
  - clang-tidy: sources are our entries in `build/debug/compile_commands.json` (`BUILD_DIR`; Phase 3 changes it), deduplicated (test sources are compiled twice). With `-- files`, only the sources those files affect, via `ninja -t deps`; a non-C++ file selects all. Changing `bitset.h` selects 19 of 41 sources. A full run takes about 30 s.
  - clang-tidy is called as `$(brew --prefix llvm)/bin/clang-tidy` by full path. llvm's `bin` isn't put on `PATH`, because it also contains clang-format 23, which would shadow the pinned 20.1.7.
  - clang-tidy 23 writes `[n/N] Processing file` progress to stderr, which garbled the output of parallel runs. `tidy_batch` collects each run's output, filters out the progress lines and writes the rest at once.
- [ ] **`.pre-commit-config.yaml`:**
  - From `pre-commit-hooks`, as in Runlight: `trailing-whitespace` (with `--markdown-linebreak-ext=md`), `end-of-file-fixer`, `mixed-line-ending --fix=lf`, `check-merge-conflict`, `check-case-conflict`, `check-yaml`, `check-json`, `check-added-large-files --maxkb=500`, `check-executables-have-shebangs`, `detect-private-key`.
  - Local hooks, one per `quality.sh` stage, `verbose: true`, each with a `files:` filter.
  - Exclude `compressor_example/samples/`, which holds binary sample files (`flower.png` is 720 KB).
  - Consider `pre-commit/mirrors-clang-format` pinned to 20.1.x, so Linux, Windows and CI format exactly like this Mac.
- [ ] **Reformat the whole tree** in its own commit (`style: apply clang-format to the whole tree`). Then add that commit's hash to `.git-blame-ignore-revs` and set `git config blame.ignoreRevsFile .git-blame-ignore-revs`. The user makes the commit, so this is two steps.
- [ ] **Claude format hook:** `.claude/hooks/format-edited-file.sh`, copied from FeatherOS and adapted (C++ through clang-format, `.sh` through shfmt; never blocks an edit). Register it as a `PostToolUse` hook on `Edit|Write` in `.claude/settings.json`, which also allows `bash tools/quality.sh check`.
- [ ] **Docs:** a "Code quality" section in `docs/development.md` (`brew install pre-commit llvm shellcheck shfmt`, `pre-commit install`, `quality.sh` usage). One line in `CLAUDE.md` Commands. Workflow rule: get the code lint-clean before running tests.

Done when `pre-commit install` is in place and `pre-commit run --all-files` passes.

### Phase 1: Safety-net tests

- [ ] **Round-trip test:** for every file in `compressor_example/samples/raw/`, encode, decode and compare with the original byte for byte.
- [ ] **Edge-case tests**, added as `DISABLED_` until Phase 2 fixes them: empty input, single-symbol input (`aaaa`), a missing input file, an output file that doesn't exist yet.

### Phase 2: Bug fixes

Fix only the issues in code that survives the modernization. Each fix enables the matching `DISABLED_` test.

- [ ] `DataWriter::open`: use `std::ios::out | std::ios::trunc | std::ios::binary`.
- [ ] `Compressor::encode/decode`: check the result of `open()`, print an error, return a non-zero exit code.
- [ ] CLI with neither `encode` nor `decode`: print the help, return non-zero.
- [ ] Empty input: return early with an empty dictionary and payload.
- [ ] Single-symbol input: when the root is a leaf, give it the code `0`.
- [ ] `Engine::encode`: clear the dictionaries at the start.
- [ ] Delete `Engine::find_path` (wrong and unused).
- [ ] Docs: `configure.sh` → `generate.sh` in `docs/development.md`; add `Testing/` to `.gitignore`.

Not fixed here, because later phases replace the code: the argparser bugs (Phase 4), file-format portability and decoding speed (Phase 5).

### Phase 3: Build, tooling and platforms

Behaviour stays the same; the Phase 1 tests prove it.

- [ ] **CMake:** raise `cmake_minimum_required` to 3.25 or later (needed for presets v6). Set C++23 with `target_compile_features(... cxx_std_23)` instead of `-std=c++1z` in `CMAKE_CXX_FLAGS`.
- [ ] **Flags per target:** warnings go on our own targets only, through an interface target (`-Wall -Wextra -Wpedantic`, `/W4` on MSVC). `-Werror` becomes an option, on in CI. Remove the global `-O3` so Debug builds aren't optimized. ASan becomes a preset. `PROFILING` becomes a CMake option. Remove the forced `x86_64` architecture for Xcode.
- [ ] **vcpkg:** `vcpkg.json` with `gtest`, `benchmark` and `boost-dynamic-bitset`. Link Boost only into `compressor_benchmark` and `compressor_test` (`bitset.cpp` compares against `boost::dynamic_bitset`). Delete `dependencies/` and the ExternalProject downloads.
- [ ] **Presets:** `CMakePresets.json` with `debug`, `release`, `asan` and `xcode` configure presets, plus matching build and test presets. Replace `generate.sh` and `build.sh`, and update `docs/development.md` and `CLAUDE.md`.
- [ ] **Quality tools on the new build:** point `tools/quality.sh` and the clang-tidy hook at the preset build directory.
- [ ] **CI:** GitHub Actions on macOS first, then Linux (GCC and Clang) and Windows (MSVC), all through the presets, plus `pre-commit run --all-files`. Expect MSVC to need its own warning flags and fixes for binary-mode file I/O.

### Phase 4: CLI11

- [ ] Add `cli11` to `vcpkg.json`. `encode` and `decode` become CLI11 subcommands with `-i/--input` (validated with `CLI::ExistingFile`), `-o/--output`, `--help` and `--version`, and proper exit codes.
- [ ] Delete `argparser/`, its tests and the `IArgparser` mock. CLI tests check how a given argv is parsed instead of using mocks.

### Phase 5: Core codec redesign

The goal is a core that could run in a kernel or an experimental OS (see "Compressed file system" and "Compressed memory" under Features).

- [ ] **Freestanding core:** no iostreams, exceptions or RTTI, and ideally no memory allocation while compressing. Errors use `std::expected`. API:

  ```cpp
  size_t compress(std::span<const std::byte> in, std::span<std::byte> out);
  size_t decompress(std::span<const std::byte> in, std::span<std::byte> out);
  size_t max_compressed_size(size_t input_size);   // worst-case bound
  ```

- [ ] **Layers:** file handling and the CLI move into separate layers on top of the core. CI builds the core with `-fno-exceptions -fno-rtti` to keep it freestanding.
- [ ] **Data structures:** an array-based Huffman tree with indices replaces the `shared_ptr`/`weak_ptr`/`dynamic_pointer_cast` tree. Code tables become `std::array<Code, 256>`, and decoding uses a lookup table instead of `std::map<bitset, uint8_t>`.
- [ ] **Canonical Huffman:** store only the code lengths instead of the full dictionary. This changes the file format.
- [ ] **Benchmarks:** compare the new codec with the old one on the sample files (ratio and speed).

After Phase 5, continue with the features in their suggested order, starting with the container format.

## Known issues

Each issue names the phase that handles it.

### Correctness

- [ ] **Output file must already exist** (Phase 2). `DataWriter::open` ([writer.h](../compressor/include/compressor/data/writer.h)) opens its `std::ofstream` with `std::ios::in | std::ios::binary`. Because of `in`, the open fails when the output file doesn't exist, and an existing file isn't truncated, so leftover bytes from a longer earlier file stay at the end.
- [ ] **Open failures are ignored** (Phase 2). `Compressor::encode/decode` ([compressor.cpp](../compressor/src/compressor/compressor.cpp)) ignore the `bool` that `open()` returns and go on to read from or write to a stream that failed to open.
- [ ] **Empty input is undefined behaviour** (Phase 2). If the input is empty, `Engine::build_tree` ([engine.cpp](../compressor/src/compressor/engine/engine.cpp)) calls `_nodes.front()` on an empty vector.
- [ ] **Input with only one distinct byte is lost** (Phase 2). For input like `aaaa`, the root is the only node and its code is an empty bitset. The encoded payload is 0 bits and decoding returns nothing.
- [ ] **`Engine::find_path` returns wrong codes** (Phase 2, delete). Inside the loop it checks `node` instead of `innerNode`, so every step pushes the same bit. Nothing calls it, but it is part of `IEngine`.
- [ ] **`Engine` state isn't reset** (Phase 2). `_symbols_dict` and the byte/bit dictionaries carry over between `encode()` calls. Each task currently creates its own `Engine`, which hides this.
- [ ] **Argparser can read past the end of `argv`** (Phase 4, replaced). In `Argparser::parse` ([argparser.cpp](../argparser/src/argparser/argparser.cpp)), a value argument given as the last token (`compressor_app encode --input`) makes `inputs_[++i]` index out of range.
- [ ] **Numeric parsing is lossy** (Phase 4, replaced). `ValueParser<uint32_t/uint64_t>` ([valueparser.cpp](../argparser/src/argparser/valueparser.cpp)) uses `std::atoi`, which overflows above `INT_MAX` and quietly turns invalid input into 0.
- [ ] **The CLI does nothing silently** (Phase 2). If neither `encode` nor `decode` is given, `CLI::run` exits with code 0 and prints no error or help.

### File format

- [ ] **Files aren't portable** (Phase 5 and the container format). The dictionary size and every `bitset` length are written as raw `size_t` in native byte order ([data.cpp](../compressor/src/compressor/data/data.cpp), [bitset.cpp](../compressor/src/compressor/bitset/bitset.cpp)). There is no magic number or version field.
- [ ] **No spec** (container format). [file_format.md](file_format.md) and [compression.md](compression.md) contain only a heading.

### Performance and limitations

- [ ] **Slow decoding** (Phase 5). Each bit is pushed into a new `bitset`, followed by a `std::map<bitset, uint8_t>` lookup.
- [ ] **Everything is in memory, single-threaded** (container format with blocks).

### Build and tooling

- [ ] **Wrong script name in the docs** (Phase 2). [development.md](development.md) says `./configure.sh`; the script is `./generate.sh`.
- [ ] **Bitset benchmarks truncate their size** (Phase 5 benchmarks). `compressor_benchmark/src/bitset/*.cpp` store `state.range(0)` (up to 1024) in a `uint8_t`, so the larger runs measure sizes modulo 256.
- [ ] **Benchmark builds aren't reproducible** (Phase 3). google benchmark is downloaded from `master.zip`, not a pinned tag.
- [ ] **Dependencies are downloaded during configure** (Phase 3). googletest and benchmark come from ExternalProject; Boost (1.81 or later) must be installed separately (`brew install boost`).
- [ ] **`Testing/` isn't gitignored** (Phase 2).
- [ ] **Global compiler flags** (Phase 3). `-std=c++1z -Wall -Wextra -Werror` go into the global `CMAKE_CXX_FLAGS`, so `-Werror` also applies to googletest and benchmark. `-O3` is added globally, even to Debug builds, when ASan is off.
- [ ] **Hard-coded Xcode architecture** (Phase 3). With the Xcode generator, `CMAKE_OSX_ARCHITECTURES` is forced to `x86_64`, so Apple Silicon Macs build Intel code that runs under Rosetta.

## Features

Ideas for after the basics are done. Each section is roughly in order of difficulty.

### Container format and streaming

The foundation for most features below, so design it first.

- [ ] **File header:** magic number, format version, algorithm/pipeline ID, original size and a checksum (CRC32 or xxHash).
- [ ] **Block-based streaming:** compress fixed-size blocks independently so memory use stays constant, blocks can be compressed on separate threads, and blocks can be read individually.
- [ ] **stdin/stdout support**, so the tool works in shell pipelines (`tar c dir | compressor encode > x`).
- [ ] **Incompressible blocks:** store a block uncompressed (with a flag) if compression doesn't shrink it.

### Folder compression

- [ ] **Archive of a whole folder**, storing paths, permissions, modification times and symlinks.
- [ ] **Choose the layout:**
  - per-entry like `.zip`: each file compressed separately, plus an index. Allows listing, extracting one file and searching.
  - solid like `.tar.gz`: everything joined into one stream. Compresses better, but the whole stream has to be decompressed to read any file.

  Per-entry, or blocks plus an index, fits better with search.
- [ ] **Commands:** `list`, `extract <path>`, `cat`, `verify`, and `stats` (entropy, byte histogram, Huffman tree as Graphviz).
- [ ] **Deduplication:** store identical files only once.

### Well-known output formats

- [ ] **Own Deflate encoder** that produces valid `.gz` and `.zip` files macOS and other tools can open. Deflate is LZ77 followed by Huffman coding, and the encoder chooses the Huffman tables, so the compression still uses our own Huffman code.

### Algorithms

- [ ] **Pluggable pipeline:** zero or more transforms followed by an entropy coder, with the pipeline recorded in the header.
- [ ] **Transforms:** RLE, delta encoding, move-to-front, BWT (BWT + MTF + RLE + Huffman is the bzip2 approach).
- [ ] **Dictionary methods:** LZ77/LZSS, LZW.
- [ ] **Entropy coders:** adaptive Huffman, arithmetic coding, rANS/tANS.
- [ ] **Auto mode:** try several pipelines per block and keep the smallest.
- [ ] **Pre-trained dictionaries/tables** for many small similar inputs (like `zstd --train`).

### Search inside compressed data

- [ ] **zgrep-style search:** decompress as a stream and match while reading.
- [ ] **Block index search:** jump to the relevant block, decompress only that, and report matches with file and offset.
- [ ] **Search without decompressing:** encode the pattern with the file's Huffman table and match it directly in the bit stream. A match is only valid if it starts at a code boundary, which is the hard part.
- [ ] **FM-index:** BWT-based full-text index whose search time doesn't depend on the file size.
- [ ] **macFUSE mount**, so `grep`, `rg` and Finder work on an archive unchanged.

### Data other than text

- [ ] **Prediction transforms before entropy coding:** PNG-style filters for raw images (BMP, PPM), delta encoding for audio (WAV) and numeric data (CSV, logs). Already-compressed formats such as JPEG and PNG won't shrink further.
- [ ] **Detect the file type** and pick a suitable transform automatically.

### Compressed file system

Existing examples: squashfs, EROFS and cramfs (read-only); btrfs, ZFS, APFS and NTFS (transparent compression in read-write file systems).

- [ ] **Read-only compressed image** like squashfs: a tool builds the image from a folder, and a reader mounts it, first through macFUSE and later inside an experimental OS.
- [ ] **Block size trade-off:** compress in fixed-size pieces (4–128 KB) so programs can read at arbitrary offsets. Bigger pieces compress better; smaller pieces read faster.
- [ ] **Read-write support (later):** a small write means decompressing, changing and recompressing a whole piece, and its new compressed size needs space allocated for it.

### Compressed memory

Existing examples: macOS compressed memory, Linux zram and zswap, Windows memory compression. Pages that haven't been used recently are compressed instead of written to disk.

- [ ] **Page-sized compression (4 KB) with very fast decompression.** Reference points: LZ4, LZO and WKdm (used by macOS, designed for memory contents).
- [ ] **Patterns in memory contents:** zero-filled pages stored as a flag, pointers that share high bits, small integers. A custom algorithm for page contents is a possible research direction.
- [ ] **Huffman for pages:** a code table per 4 KB page costs too much space, so try a fixed table trained on typical memory contents. Benchmark canonical Huffman and tANS against LZ4.
- [ ] **Allocator for compressed pages of different sizes** that packs them tightly (Linux uses zsmalloc).
- [ ] **User-space experiment first:** collect real memory pages (Linux `/proc/<pid>/mem` or a QEMU snapshot) and measure compression ratio and speed per algorithm.

### Tooling

- [ ] **Benchmark harness:** compression ratio and speed compared with gzip, zstd and xz on standard test sets (Canterbury, Silesia), tracked over time.
- [ ] **Fuzzing the decoder** with libFuzzer, so a crafted file can't crash it or corrupt memory.
- [ ] **Interactive web demo:** compile to WebAssembly and show the tree being built and the bit stream for some input text.
- [ ] **Python bindings** via pybind11.

### Suggested order

1. Container format with streaming and blocks.
2. Folder support and the extra commands.
3. An LZ77 pipeline next to Huffman.
4. Deflate output for zip/gzip compatibility.
5. Searching through the block index.
6. Read-only compressed file system image, and the compressed-memory experiments.
7. Advanced options: FM-index, FUSE mount, type-specific transforms.

The benchmark harness and fuzzing can be added alongside any of these steps.
