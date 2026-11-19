# Roadmap

The phased plan for getting the basics right, the known issues it addresses, and feature ideas for afterwards.

## Decisions

- **Dependencies:** vcpkg in manifest mode (`vcpkg.json` with a pinned `builtin-baseline`), using the toolchain file from `$VCPKG_ROOT`, set in the root `CMakeLists.txt` (not in presets or on the command line). Third-party code comes only through vcpkg, not `FetchContent` or `add_subdirectory`, so every target built in this tree is ours and can take global compile flags. A library vcpkg lacks gets an overlay port. Every port, transitive and host ones included, is pinned to its exact version (and port-version) in `vcpkg.json`'s `overrides`; configure fails when an installed port has no matching override (`cmake/VcpkgPins.cmake`).
- **Language:** C++23. Apple Clang 17 (Xcode 26) supports `<expected>`, `<print>` and `<span>`; checked 2026-11-08.
- **Argument parsing:** CLI11 replaces our own `argparser/`.
- **Platforms:** macOS first, then Linux and Windows.
- **File format:** breaking compatibility with existing compressed files is fine.

## Phased plan

Each phase is one or more steps, and every step should end with tests passing. When a step is done, mark it `(done)` and replace its plan text with what was actually built.

### Phase 0: Pre-commit hooks and code quality (done)

Every commit now runs clang-format, clang-tidy, shellcheck, shfmt and the generic `pre-commit-hooks` through `tools/quality.sh`, the whole tree is formatted and lint-clean, and Claude's edits are formatted by a hook. Setup and usage are in `docs/development.md` ("Code quality").

Set up before any other coding, following FeatherOS (`tools/quality.sh` with one pre-commit hook per stage, a Claude format hook) and Runlight (the generic `pre-commit-hooks` set). Installed: `pre-commit`, `shellcheck`, `shfmt`, and `clang-format` and `clang-tidy` 23.1.3 from `brew install llvm` (`/opt/homebrew/opt/llvm/bin`, not on `PATH`; it also has `run-clang-tidy` and `clang-apply-replacements`). The tree was first formatted with Homebrew's separate `clang-format` 20.1.7; the switch to llvm's 23 (last bullet) changed no file.

- [x] **Build check (done):** CMake 4.4.5 and Apple Clang 17 configure and build Debug, Release and the Xcode project with `-Werror`; 74/74 tests pass. `cmake_minimum_required(VERSION 3.10)` is still accepted. Changes needed:
  - Boost: CMake 4 removed the `FindBoost` module (CMP0167), so the root `CMakeLists.txt` now calls `find_package(Boost 1.81.0 REQUIRED CONFIG)` and `compressor_benchmark` and `compressor_test` link `Boost::headers`. The `dependencies/boostorg/` wrapper and the unused `${Boost_INCLUDE_DIR}` on other targets are gone. Homebrew ships Boost 1.92, which works.
  - `CMAKE_EXPORT_COMPILE_COMMANDS ON` in the root `CMakeLists.txt`; `build/debug/compile_commands.json` exists after `./generate.sh`.
  - The googletest/benchmark download templates (`dependencies/google/*/CMakeLists.txt.in`) set `DOWNLOAD_EXTRACT_TIMESTAMP TRUE` (CMP0135) and require CMake 3.25 (CMP0114 warning under Xcode).
  - Left as is: googletest 1.13's own `cmake_minimum_required` deprecation warnings and `ld: warning: ignoring duplicate libraries` (gtest and benchmark linked twice via `package_add_test`). Both go away with vcpkg in Phase 3.
- [x] **Config files (done):** `run-clang-tidy -p build/debug` over the whole tree passes; Debug and Release build with `-Werror`; 74/74 tests pass.
  - `.editorconfig`: UTF-8, LF, final newline, trimmed trailing whitespace, 4-space indent; 2 spaces for YAML/JSON; Markdown keeps trailing whitespace; shfmt options for `.sh` (`binary_next_line`, `switch_case_indent`, `space_redirects`, as in FeatherOS).
  - `.clang-format`: FeatherOS's (LLVM base, 4 spaces, 100 columns, `PointerAlignment: Left`) with changes: Allman braces except lambda bodies (`BeforeLambdaBody: false`, `LambdaBodyIndentation: OuterScope`), leading-comma constructor initializers one per line (`BreakConstructorInitializers: BeforeComma`, `PackConstructorInitializers: Never`, as the code already did), and includes sorted within their existing blocks (`SortIncludes: CaseSensitive`, `IncludeBlocks: Preserve`). The tree still has about 750 formatting differences; the reformat step below fixes them (and must confirm the sorted includes still build).
  - `.clang-tidy`: `bugprone-*`, `clang-analyzer-*`, `performance-*`, `modernize-*`, `WarningsAsErrors: '*'`. Header filter is everything except `build/` (downloaded googletest/benchmark) and `argparser/`. Off for good: `bugprone-easily-swappable-parameters`, `modernize-use-nodiscard`, `modernize-use-trailing-return-type`. Off until Phase 5 (all findings sit in bitset, the tree and `Engine`; the comment in the file says to turn them back on): `bugprone-signed-bitwise`, `bugprone-narrowing-conversions`, `bugprone-exception-escape` (also fires on the two example `main`s, which will need a try/catch), `bugprone-derived-method-shadowing-base-method`. (On again since Phase 5's "Encode bit buffer" step.)
  - Nested configs (`InheritParentConfig: true`): `argparser/.clang-tidy` turns off the eight checks that fire there, and Phase 4 deletes it with `argparser/`; `compressor_benchmark/.clang-tidy` turns off `clang-analyzer-deadcode.DeadStores` and `bugprone-throwing-static-initialization` for google benchmark's `for (auto _ : state)` and `BENCHMARK()` idioms. A nested config doesn't apply to its headers when another package's file includes them, hence the `argparser/` header exclusion in the root file.
  - Fixed: `override` instead of `virtual` on overrides, `= default`, pass-by-value plus `std::move` for sink parameters (task, node, tree and iterator constructors/setters), const-ref lambda parameters in `Engine`, `static_cast` for C-style casts, nested namespace `compressor::utils`, `std::make_unique`, `std::scoped_lock`, raw string literals in `Instrumentor`, `noexcept` on `bitset`'s move assignment. `compressor_example` no longer has throwing globals. In `bitset` tests: the moved-from check carries a `NOLINT`, `out4` is initialized, `in4` is written as `char`. `Engine::print_dict` casts the byte to `char` explicitly (same output).
  - `.shellcheckrc`: `external-sources=true`. `generate.sh` and `build.sh` pass shellcheck; shfmt wants one blank line removed in `generate.sh` (reformat step).
- [x] **`tools/quality.sh` (done)**, adapted from FeatherOS's. On the current tree `clang-tidy` and `shellcheck` pass; `clang-format` and `shfmt` fail until the reformat step. Tested on a scratch copy of the repo: a C-style cast fails `check clang-tidy`; `--fix` fixes it and then passes every stage; after `format`, all four stages pass, so the reformatted tree is clang-tidy clean as well.
  - Stages `clang-format`, `clang-tidy`, `shellcheck`, `shfmt`. Commands `check [stage...] [-- file...]`, `--fix` (format, then serial `clang-tidy --fix --warnings-as-errors='-*'`, then check every stage), `format`, `stages`. More than one stage prints `ok`/`FAIL` with timing per stage and a total; a single stage (one pre-commit hook) prints only diagnostics.
  - Files come from `git ls-files --cached --others --exclude-standard` (`*.cpp *.h *.hpp`, `*.sh`), so new files are covered and `build/` is skipped. shfmt takes its options from `.editorconfig` (no flags on the command line).
  - clang-tidy: sources are our entries in `build/debug/compile_commands.json` (`BUILD_DIR`; Phase 3 changes it), deduplicated (test sources are compiled twice). With `-- files`, only the sources those files affect, via `ninja -t deps`; a non-C++ file selects all. Changing `bitset.h` selects 19 of 41 sources. A full run takes about 30 s.
  - clang-tidy is called as `$(brew --prefix llvm)/bin/clang-tidy` by full path, since llvm is keg-only. (clang-format later moved to llvm as well, see the last bullet of this phase.)
  - clang-tidy 23 writes `[n/N] Processing file` progress to stderr, which garbled the output of parallel runs. `tidy_batch` collects each run's output, filters out the progress lines and writes the rest at once.
- [x] **`.pre-commit-config.yaml` (done)**, not installed yet: until the reformat step, the clang-format and shfmt hooks would block every commit. Tested on a scratch clone with pre-commit 3.7.1: `pre-commit run --all-files` fails only clang-format and shfmt, plus the whitespace fixers' first run; after `quality.sh format`, every hook passes. In the real repo, `pre-commit run clang-tidy --files compressor/include/compressor/bitset/bitset.h` checks 19 of 41 sources in about 23 s, and a Markdown file skips the hook.
  - `pre-commit-hooks` v5.0.0, Runlight's set: `trailing-whitespace --markdown-linebreak-ext=md`, `end-of-file-fixer`, `mixed-line-ending --fix=lf`, `check-merge-conflict`, `check-case-conflict`, `check-yaml` (also checks `.clang-format`/`.clang-tidy`), `check-json`, `check-added-large-files --maxkb=500`, `check-executables-have-shebangs`, `detect-private-key`. On the current tree, the two fixers change 44 files (192+/203−), which go into the reformat commit.
  - Local hooks, one per `quality.sh` stage, `language: system`, `verbose: true`. `clang-format`, `shellcheck` and `shfmt` run on the whole tree (`pass_filenames: false`) and are triggered by C++ files plus `.clang-format`, by `.sh` plus `.shellcheckrc`, and by `.sh` plus `.editorconfig`. `clang-tidy` gets the staged files (`check clang-tidy --`, `require_serial`) and is triggered by C++ files, any `.clang-tidy`, `CMakeLists.txt` and `*.cmake`.
  - Global `exclude: ^compressor_example/samples/` (binary images, and lorem texts whose bytes matter).
  - No `mirrors-clang-format`: it would only cover the hook, not `quality.sh format` or the Claude hook. Instead, `quality.sh` fails if the clang-format version isn't major `CLANG_FORMAT_MAJOR` (`require_clang_format_version`, called from `require_tools`). Tested with a fake clang-format of another major version: `check clang-format` and `format` fail before touching any file, and `check shellcheck` still passes. CI (Phase 3) can install the matching `clang-format` from PyPI on every platform.
- [x] **Reformat the whole tree (done)**, to be committed on its own as `style: apply clang-format to the whole tree`: 73 files, 714+/697−. `quality.sh format` changed 64 files (clang-format, plus the blank line shfmt removes from `generate.sh`); the first `pre-commit run --all-files` then fixed trailing whitespace in the two `dependencies/google/*/CMakeLists.txt` and missing final newlines in 11 files (CMake files, `readme.md`, `docs/compression.md`, `docs/file_format.md`, `instrumentationtimer.*`, `coder.cpp`). The second run passes every hook, clang-tidy included (41 of 41 sources). The sorted includes build: Debug and Release build with `-Werror`, 74/74 tests pass.
- [x] **Install the hooks (done):** `pre-commit install` wrote `.git/hooks/pre-commit`. `.git-blame-ignore-revs` lists the reformat commit `3db9c77001859160d521d4bc7932946b81fc68f3` under a `# style: apply clang-format to the whole tree` comment, and `git config blame.ignoreRevsFile .git-blame-ignore-revs` is set in this clone (each new clone needs it; the Docs step documents it). `git blame` skips the commit: in `engine.cpp` it goes from 13 lines to none. In `argparser.cpp` it goes from 27 lines to 11, all of them blank lines that have no earlier version to fall back to. `pre-commit run --all-files` passes every hook (clang-tidy 41 of 41 sources, about 28 s); 74/74 tests pass.
- [x] **Claude format hook (done):** `.claude/hooks/format-edited-file.sh` is a `PostToolUse` hook on `Edit|Write` in `.claude/settings.json` (timeout 30 s), which also allows `bash tools/quality.sh check` with or without arguments. The hook reads `tool_input.file_path` with `python3`, skips files outside the repo and anything but `*.cpp *.h *.hpp *.sh`, and runs `quality.sh format -- <file>` with its output hidden; it always exits 0, so it never blocks an edit.
  - Unlike FeatherOS's hook, it doesn't call the formatters itself: FeatherOS's puts llvm's `bin` first on `PATH` and hard-codes shfmt flags, while `quality.sh` already knows which clang-format to use and checks its version. Instead `quality.sh format` got an optional `-- file...`: it formats only the given files that `repo_files` lists (so not `build/` or other ignored files), C++ with clang-format and `.sh` with shfmt, after the same version check. `format --` with no files does nothing; plain `format` still formats the whole tree.
  - Tested by feeding the hook JSON: a badly formatted `.cpp` (path with spaces) and `.sh` in the repo are formatted; a file under `build/`, one outside the repo, a `.txt`, invalid JSON, and a clang-format of the wrong major version all leave the file unchanged and exit 0. It also fired live on a `Write` in the session that added it. Plain `format` on the tree changes nothing. 74/74 tests pass.
- [x] **Docs (done):** `docs/development.md` has a "Code quality" section: setup (`brew install pre-commit llvm shellcheck shfmt`, `pre-commit install`, `git config blame.ignoreRevsFile .git-blame-ignore-revs`), where clang-format and clang-tidy come from and how to bump the pinned clang-format major version, `quality.sh` usage including `format -- file...`, the pre-commit hooks, and the Claude format hook. `CLAUDE.md` Commands has `bash tools/quality.sh check`; its Workflow says to get the code lint-clean before running the tests. `pre-commit run --all-files` passes every hook; 74/74 tests pass.
- [x] **clang-format 23 (done):** `quality.sh` takes clang-format from Homebrew's `llvm` by full path (`CLANG_FORMAT`, resolved like `CLANG_TIDY`; plain `clang-format` from `PATH` without Homebrew), and `CLANG_FORMAT_MAJOR=23`, so one `brew install llvm` provides both tools at the same version. clang-format 23.1.3 leaves the tree unchanged, so no reformat commit. The separate `clang-format` 20.1.7 formula can be uninstalled. Tested: every stage passes; a fake llvm whose clang-format is 20 makes `check clang-format` and `format` fail; the Claude hook formats a scratch `.cpp` through llvm's clang-format.

Done when `pre-commit install` is in place and `pre-commit run --all-files` passes.

### Phase 1: Safety-net tests (done)

End-to-end round-trip tests over the sample files pass, and every Phase 2 correctness bug has a `DISABLED_` test that fails today; 81/81 tests pass with 7 disabled.

- [x] **Round-trip test (done):** `compressor/test/src/roundtrip/roundtrip.cpp`. `RoundTripTests` is parameterized over every regular file in `compressor_example/samples/raw/` (listed at static initialization with the `error_code` overloads, sorted; test names are the file names with non-alphanumerics replaced by `_`, e.g. `Samples/RoundTripTests.Encode_then_decode_restores_the_file/lena_jpg`), so a new sample gets a test without code changes. Each test goes end to end through `Compressor` with a real `EncoderTask` and `DecoderTask` and files in `$TMPDIR/compressor_roundtrip_<name>/`, then compares the decoded file with the original byte for byte. `RoundTripSamplesTests.Samples_directory_is_not_empty` fails if the directory is missing or empty. The path comes from the `COMPRESSOR_SAMPLES_DIR` compile definition in `compressor/test/CMakeLists.txt`.
  - The test creates both output files before encoding, because `DataWriter::open` can't create one yet (known issue below). When Phase 2 fixes it, remove `touch()`; the edge-case test covers the missing-output case.
  - All six samples (two images, four lorem texts) round-trip; 81/81 tests pass, the whole suite in about 7 s.
- [x] **Edge-case tests (done):** seven `DISABLED_` tests, each failing today (checked with `--gtest_also_run_disabled_tests --gtest_filter=<name>`, one at a time, because two crash). Phase 2 enables the matching test with each fix.
  - `EdgeCaseTests` in `roundtrip.cpp` (real files in `$TMPDIR/compressor_edgecase_<test>/`): `Empty_input_round_trips_to_an_empty_file` (segfault), `Single_symbol_input_round_trips` (`aaaa`), `Encoding_a_missing_input_leaves_the_output_untouched` (segfault), `Output_files_are_created_when_missing`, `Existing_longer_output_files_are_truncated`.
  - `CompressorTests` in `compressor/compressor.cpp` (mocks): `Encoding_stops_when_the_input_cannot_be_opened` and `Decoding_stops_when_the_input_cannot_be_opened`. They expect no `read()`, no engine call and no writer `open()`/`write()` after the reader's `open()` returns `false`. An end-to-end test for decoding a missing input passed by accident (empty data written over an existing file leaves it unchanged), so the mock test covers that case.
  - `Output_files_are_created_when_missing` asserts that the encoded file exists before it decodes, because decoding a missing file currently hangs: `operator>>(istream&, EncodedData&)` loops over a `dict_size` that was never read.
  - When Phase 2 makes `Compressor` check `open()`, the existing `CompressorTests.Encoding`/`Decoding` need `WillOnce(Return(true))` on the reader and writer `open()`: the mock default `false` would stop the pipeline.

### Phase 2: Bug fixes (done)

The Phase 2 known issues are fixed, all seven Phase 1 `DISABLED_` tests are enabled and pass, and `compressor_app` returns a non-zero exit code on failure; 91/91 tests pass, none disabled.

- [x] **`DataWriter::open` (done):** opens with `std::ios::out | std::ios::trunc | std::ios::binary`. `DataReader`/`DataWriter::open` no longer print; `Compressor` reports the error. The round-trip tests no longer `touch()` their outputs. Covered by `EdgeCaseTests.Output_files_are_created_when_missing` and `Existing_longer_output_files_are_truncated`.
- [x] **Open failures (done):** `ICompressor::encode/decode` return `bool`. `Compressor` stops right after a failed reader `open()` (no read, no engine call, no writer call) and prints `Cannot open input file: <path>` to `stderr`; a failed writer `open()` prints `Cannot open output file: <path>` and returns `false` without writing. Unlike the plan, the writer is still opened after reading and encoding, not before reading, so an input given as its own output isn't truncated before it is read. Covered by `CompressorTests.{Encoding,Decoding}_stops_when_the_input_cannot_be_opened`, `{Encoding,Decoding}_fails_without_writing_when_the_output_cannot_be_opened` and `EdgeCaseTests.Encoding_a_missing_input_leaves_the_output_untouched`.
- [x] **CLI exit codes (done):** `CLI::run()` returns `EXIT_SUCCESS`/`EXIT_FAILURE` and `main` returns it. With neither `encode` nor `decode`, it calls `print_help()` and fails. Covered by `CLITests.EmptyCommand` and `Failed_encoding_gives_a_non_zero_exit_code`; the empty `CLITests.EmptyFiles` was removed. Side effect: `compressor_app --help` now prints the help twice (once in `Argparser::parse`, once in `run`) and exits 1; Phase 4 replaces this.
- [x] **Empty input (done):** `Engine::encode` returns an empty `EncodedData` (no dictionary, 0-bit payload) before building a tree; decoding it gives an empty file. Covered by `EdgeCaseTests.Empty_input_round_trips_to_an_empty_file`.
- [x] **Single-symbol input (done):** in `Engine::create_hash_table`, a root that is also a leaf gets the code `0`. Covered by `EdgeCaseTests.Single_symbol_input_round_trips`.
- [x] **Engine reset (done):** `Engine::encode` resets `_tree`, `_symbols_dict`, `byte_dict_` and `bit_dict_` first. Covered by `EngineTests.Encoding_twice_gives_the_same_result_as_a_fresh_engine` (checked to fail without the reset).
- [x] **`find_path` (done):** removed from `IEngine`, `Engine` and `MockIEngine`.
- [x] **Docs (done):** `docs/development.md` says `./generate.sh`; `Testing` is in `.gitignore`.

Not fixed here, because later phases replace the code: the argparser bugs (Phase 4), file-format portability and decoding speed (Phase 5).

### Phase 3: Build, tooling and platforms

Behaviour stays the same; the Phase 1 tests prove it.

- [x] **CMake (done):** the root `CMakeLists.txt` has `cmake_minimum_required(VERSION 3.25)` and `CMAKE_CXX_EXTENSIONS OFF`; `-std=c++1z` is gone from `CMAKE_CXX_FLAGS`. `argparser`, `instrumentor`, `compressor` and `compressor_cli` declare `target_compile_features(... PUBLIC cxx_std_23)`, which reaches every executable and test target through linking, so all 56 of our compile commands get `-std=c++23`. googletest and benchmark keep their own `-std=c++17`. Debug and Release build with `-Werror` and pass 91/91; the Xcode project builds.
  - libc++ in C++23 mode drops transitive includes: added `<memory>` to `tree/binary/node.h`, `<algorithm>` and `<iterator>` to `data/data.cpp`, `<algorithm>` to the `nodeiterator.cpp` test.
  - C++20/23 turns on more `modernize-*` checks; fixed with `quality.sh --fix`: `std::ranges::{copy,sort,transform,replace,replace_if}` in `data.cpp`, `engine.cpp`, `valueparser.cpp`, `instrumentor.cpp` and `roundtrip.cpp`; `starts_with` in `argparser.cpp`; designated initializers for `ProfileResult` in `instrumentationtimer.cpp`.
  - `BitsetTests.Copying` keeps `std::copy` with a `NOLINT(modernize-use-ranges)`: `bitset_iterator` has no `difference_type`/`value_type`, so `bitset` isn't a `std::ranges::range`. Phase 5's bitset rewrite should make it one and drop the NOLINT.
- [x] **Flags per target (done):** `cmake/Warnings.cmake` defines the `compressor_warnings` interface target (`-Wall -Wextra -Wpedantic`, `/W4` on MSVC, plus `-Werror`/`/WX` with `COMPRESSOR_WERROR`), linked `PRIVATE` into every one of our targets, including both targets of `package_add_test`. googletest gets no warning flags; benchmark keeps its own (including its own `-Werror`). The code was already `-Wpedantic` clean. Root options: `COMPRESSOR_WERROR` (off; `generate.sh` turns it on, CI will too), `COMPRESSOR_ASAN`, and `COMPRESSOR_PROFILING`, which becomes `PROFILING=0/1` as a `PUBLIC` definition on `instrumentor` (no more global `add_compile_definitions`). The forced Xcode `x86_64` is gone, so Xcode builds native arm64. Debug, Release and Xcode Debug pass 91/91, `quality.sh check` passes, and a `COMPRESSOR_PROFILING=ON` `compressor_app` writes `result.json`.
  - All `target_link_libraries` calls use the keyword form now (CMake doesn't allow mixing it with the plain one): `PUBLIC` for library dependencies as before, `PRIVATE` in executables.
  - The global `-O3` never had any effect: it was appended to `CMAKE_CXX_FLAGS` after every `add_subdirectory`, and the root directory has no targets. Debug was already `-g` only; Release gets `-O3 -DNDEBUG` from the build type.
  - `ENABLE_ASAN` became `COMPRESSOR_ASAN` with `add_compile_options`/`add_link_options` (global on purpose, so the dependencies are instrumented too). `-fsanitize=leak` is dropped: Apple Clang 17 rejects it on arm64 (so the old `ENABLE_ASAN` didn't build here), and on Linux `-fsanitize=address` already includes LSan. The old `-g -O1` is gone too; the build type decides. In Claude's sandbox every ASan binary hangs at startup (even a two-line program), so ASan runs happen outside it; verified with the `asan` preset after Phase 4.
  - Native arm64 broke Xcode builds: `gtest_discover_tests` ran its `POST_BUILD` discovery before Xcode's code-signing phase, and macOS kills unsigned arm64 binaries ("Subprocess killed"). `package_add_test` now uses `DISCOVERY_MODE PRE_TEST`, so tests are listed when `ctest` runs.
- [x] **vcpkg (done):** `vcpkg.json` (manifest mode, `builtin-baseline` pinned to vcpkg commit `d64c795e`, 2026-04-17) brings `gtest` 1.17.0, `benchmark` 1.9.5 and `boost-dynamic-bitset` 1.90.0; `generate.sh` passed `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` as `CMAKE_TOOLCHAIN_FILE` (the Presets step moved it into the root `CMakeLists.txt`), and configure installs into `<build dir>/vcpkg_installed`. The root `find_package`s `GTest`, `benchmark` and `boost_dynamic_bitset` (all `CONFIG`); targets link `GTest::gtest`/`GTest::gmock`/`GTest::gtest_main` (in `package_add_test`), `benchmark::benchmark`/`benchmark::benchmark_main` and `Boost::dynamic_bitset` (only `compressor_test` and `compressor_benchmark`). `dependencies/` is deleted. Debug, Release and Xcode Debug build with warnings as errors and pass 91/91, `quality.sh check` passes, and `compressor_benchmark` runs.
  - Global flags again: the root `add_compile_options(-Wall -Wextra -Wpedantic)` (`/W4` on MSVC); `CMAKE_COMPILE_WARNING_AS_ERROR=ON` from `generate.sh` replaces `COMPRESSOR_WERROR`. `cmake/Warnings.cmake` and every `compressor_warnings` link are gone. vcpkg headers come in as `-isystem`, so its code isn't warned about.
  - `cmake_minimum_required(VERSION 3.25...3.31)`: the policy range turns on CMP0156 (3.29+), which de-duplicates static libraries on link lines. Without it Apple ld warns "ignoring duplicate libraries" for `libgtest.a` and `libbenchmark.a`, since `gtest_main`, `gmock` and `benchmark_main` each pull their base library in again.
  - ASan: `vcpkg-configuration.json` lists `triplets/` as `overlay-triplets`, which holds `arm64-osx-asan` and `x64-linux-asan` (the stock triplets plus `-fsanitize=address -fno-omit-frame-pointer` in `VCPKG_C_FLAGS`/`VCPKG_CXX_FLAGS` and `-fsanitize=address` in `VCPKG_LINKER_FLAGS`). `COMPRESSOR_ASAN` now instruments only our code and stops configure with an error unless `VCPKG_TARGET_TRIPLET` ends in `-asan`. Windows has no ASan triplet yet (CI step). benchmark's configure runs `try_run` feature checks, and in Claude's sandbox every ASan binary hangs at startup, so `vcpkg install --triplet arm64-osx-asan` never finishes there; build it outside the sandbox.
  - `tools/quality.sh` no longer filters `build/` out of the compile database; nothing from there is compiled any more.
  - Exact pins (added after the Presets step): `overrides` lists all 16 installed ports (the 3 direct ones, 10 boost dependencies, and the host ports `vcpkg-boost`, `vcpkg-cmake`, `vcpkg-cmake-config`). `cmake/VcpkgPins.cmake`'s `compressor_check_vcpkg_pins()` runs right after `project()`: every `<port>_<version>_<triplet>.list` under `${VCPKG_INSTALLED_DIR}/vcpkg/info` needs an override with that version. Checked by dropping the `boost-mp11` override, which fails configure with "boost-mp11 1.90.0 has no override". Ports that only Linux or Windows pull in will show up there in the CI step and need adding.
- [x] **Presets (done):** the root `CMakeLists.txt` sets `CMAKE_TOOLCHAIN_FILE` to `$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` before `project()` unless one is given (another toolchain chains through `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`), and stops with an error when `VCPKG_ROOT` is unset. `CMakePresets.json` (version 6, CMake 3.25) has configure, build, test and workflow presets `debug`, `release`, `asan` and `xcode`, each in `build/<preset>`. The hidden `base` sets Ninja and `CMAKE_COMPILE_WARNING_AS_ERROR=ON`; `asan` inherits `debug` and adds `COMPRESSOR_ASAN=ON` and `VCPKG_TARGET_TRIPLET=arm64-osx-asan`; `asan` and `xcode` exist only on Darwin (`condition`). The `xcode` build and test presets use the Debug configuration; test presets print output on failure and fail when no tests are found. `generate.sh` and `build.sh` are deleted; `CMakeUserPresets.json` is in `.gitignore`. `cmake --workflow --preset debug|release|xcode` passes 91/91 each.
  - The `asan` workflow can't run inside Claude's sandbox (the ASan `try_run` checks in benchmark's configure hang, see the vcpkg step), so run it by hand. Verified on 2026-11-18, after Phase 4: `cmake --workflow --preset asan` passes 65/65.
  - Linux gets its own `asan` variant (with `x64-linux-asan`) in the CI step; presets can't pick a triplet per host, so it needs a separate preset or a `condition` per platform.
- [x] **Quality tools on the new build (done with Presets):** the `debug` preset builds in `build/debug`, which is where `tools/quality.sh` and the clang-tidy hook already read `compile_commands.json`; only the "no compile database" hint changed to `cmake --preset debug`.
- [ ] **CI (deferred, 2026-11-18):** not needed for now; Phase 4 goes ahead without it. When picked up: GitHub Actions on macOS first, then Linux (GCC and Clang) and Windows (MSVC), all through the workflow presets (with `VCPKG_ROOT` from the runner's vcpkg or `lukka/run-vcpkg`), plus `pre-commit run --all-files`. Expect MSVC to need its own warning flags and fixes for binary-mode file I/O.

### Phase 4: CLI11 (done)

The hand-written `argparser/` is replaced by CLI11; `compressor_app` has `encode`/`decode` subcommands, `--help` prints once and exits 0, and every parse error exits non-zero. 65/65 tests pass (the argparser tests are gone; 13 new CLI tests).

- [x] **CLI11 (done):** `cli11` 2.6.2 in `vcpkg.json` (with its `overrides` pin), `find_package(CLI11 CONFIG REQUIRED)` in the root, linked `PRIVATE` into `compressor_cli`, so `cli.h` doesn't include it. vcpkg's port builds CLI11 as a compiled library (`CLI11_COMPILE`).
  - `compressor::CLI(std::unique_ptr<ICompressor>, std::ostream& out = std::cout, std::ostream& err = std::cerr)` and `int run(std::span<const char* const> args)`: the `CLI::App` is built inside `run` (inside namespace `compressor`, CLI11's namespace is spelled `::CLI`). `encode` and `decode` subcommands, exactly one required, each with required `-i/--input` (`CLI::ExistingFile`) and `-o/--output`; `--version` prints `COMPRESSOR_VERSION`. Parse errors, `--help` and `--version` go through `app.exit(e, out, err)`; a failed encode/decode writes `Encoding <input> failed` to `err` and returns `EXIT_FAILURE`. The old `Input file:`/`Encode` prints are gone.
  - The version comes from `project(CompressionProject VERSION 0.0.1 ...)` as a `PRIVATE` `COMPRESSOR_VERSION` definition on `compressor_cli` (and on both CLI test targets); the unused `VERSION_MAJOR/MINOR/PATCH` cache variables are removed. Keep it in step with `vcpkg.json`'s `version`.
  - CLI11 2.6's help has no `Usage:` line; it starts with the description, then `compressor [OPTIONS] SUBCOMMAND` and an `OPTIONS:` section.
- [x] **Delete `argparser/` (done):** the directory, its tests, the `IArgparser` mock and `argparser/.clang-tidy` are gone; the root `.clang-tidy` no longer excludes `argparser/` headers. `compressor_app` and `compressor_example` call `CLI::run` with an argv span.
  - `CLITests` (`compressor_cli/test/src/cli.cpp`) run real argv through `CLI::run` with a `MockICompressor` and captured streams, and a real input file in `$TMPDIR/compressor_cli_<test>/` for `ExistingFile`: the paths reach the encoder/decoder task, failed encode/decode exit non-zero, `--help` prints the `OPTIONS:` section once and exits 0, `encode --help` and `--version` exit 0, and no subcommand, two subcommands, a missing input file, `--input` as the last token, a missing `--output` and an unknown option all fail without calling the compressor.
  - Checked by hand: `compressor_app` round-trips `lena.jpg`; a missing input exits 105, a trailing `--input` 114, no subcommand 106 (CLI11's exit codes). `cmake --workflow --preset debug|release|xcode` pass 65/65; `compressor_example` runs; `quality.sh check` passes.

### Phase 5: Benchmarks and performance (done)

Done 2026-11-19: canonical length-limited Huffman with a decode table and a 64-bit bit writer (encode 210–960 MiB/s, decode 170–340 MiB/s), block stream I/O, corrupt input reported through `std::expected`, and a stored fallback behind a method byte.

Measure first, then make the existing codec fast, one change per step, each with a before/after benchmark diff. Decided 2026-11-18: the freestanding core is postponed (Phase 6); zlib's Huffman-only mode is the reference; both the Canterbury and the Silesia corpus are used.

- [x] **Benchmark harness and baseline (done):** numbers and takeaways in [benchmarks.md](benchmarks.md); how to run in `docs/development.md` ("Run benchmark").
  - `tools/fetch-corpus.sh [canterbury|silesia...]` downloads `cantrbry.tar.gz` and `silesia.zip` into `corpus/canterbury/` and `corpus/silesia/` (git-ignored). It checks each archive's SHA-256, pinned in the script, before unpacking, and skips a corpus whose directory exists. Tested: a wrong checksum fails and leaves no temporary files; an unknown name fails.
  - `zlib` 1.3.1 in `vcpkg.json` with its `overrides` pin; `find_package(ZLIB REQUIRED)` in the root; only `compressor_benchmark` links `ZLIB::ZLIB`.
  - `compressor_benchmark` has its own `src/main.cpp` (no `benchmark_main`), which calls `compressor::bench::register_codec_benchmarks()` (`src/codec/codec.cpp`). It registers `Engine/{Encode,Decode}` and `ZlibHuffman/{Encode,Decode}` for every file in the samples, `corpus/canterbury` and `corpus/silesia` (paths from the `COMPRESSOR_SAMPLES_DIR`/`COMPRESSOR_CORPUS_DIR` definitions), named `<codec>/<op>/<set>/<file>`. A static `Prepared` caches one file at a time (input, entropy, encoded data), so a decode benchmark reuses the output of the encode benchmark that ran just before it.
    - Counters `bytes_per_second` (original bytes), `ratio`, `header` (`Engine` only) and `efficiency`. Efficiency uses the total compressed size for both codecs, not the payload as planned, because zlib's block headers can't be separated from its payload.
    - Decode benchmarks compare their output with the input and call `SkipWithError` on a mismatch. This is the harness's correctness check. Unlike the plan, it runs after the timed loop, so no extra decode is needed.
  - `Engine::encode` no longer prints `Encoded: … => …`.
  - `tools/bench.sh [--all] [--filter <regex>] [--out <file>] [-- <args>]` builds `release` and runs the benchmark with JSON output (default `build/release/benchmark.json`). Without `--all` or `--filter`, it passes `--benchmark_filter=-/silesia/`. `tools/bench-compare.py old.json new.json` prints MB/s, speedup and ratio per benchmark.
  - Bitset benchmarks take `std::size_t` sizes; the empty `Copying.cpp` is deleted.
  - Baseline: `docs/benchmarks/baseline.json`, from `tools/bench.sh --all` (host name and absolute paths removed by hand). The full run took about 3 minutes, much less than estimated; `Engine` on Silesia takes about 85 s to decode in total.
  - Main findings: decoding runs at 1.4–7.5 MiB/s (zlib: 200–1600). Efficiency on large files is 1.004–1.009, so the code itself is near-optimal. The header (up to 2.8 KB) costs 27–39% on files of a few KB. `flower.png` and `lena.jpg` grow.
  - `quality.sh check` passes; 65/65 tests pass.
- [x] **Canonical Huffman with length-limited codes (done):** the format is in [file_format.md](file_format.md), the numbers are in [benchmarks.md](benchmarks.md). Decoding still runs bit by bit.
  - `compressor/huffman/huffman.h`:
    - types `Frequencies`, `CodeLengths`, `Code {bits, length}`, `Codes`, and `max_code_length = 12`;
    - `code_lengths(frequencies, max_length)`: package-merge, building the levels deepest first, then counting the leaves in the chosen prefix of each level;
    - `canonical_codes(lengths)`: RFC 1951;
    - `valid_code_lengths(lengths)`: Kraft check, with the single-symbol and empty cases.
  - `EncodedData` is `{size_, code_lengths_, data_}`. Header: 8-byte little-endian size, then 128 bytes of nibble lengths; the payload is MSB-first. The bitset keeps bits LSB-first, so `operator<<`/`>>` convert bit by bit (temporary until the bit-buffer step). `operator>>` resets the data and sets `failbit` on a short header, invalid lengths, codes with size 0, or no codes with size > 0. The payload read back is padded to whole bytes, so a stream round trip compares equal on size and lengths but not on `data_`.
  - `Engine`:
    - `encode` counts into `Frequencies`, then uses `code_lengths` and `canonical_codes`, with one precomputed `bitset` per symbol (`code_bits_`);
    - `decode` keeps the bit-by-bit `std::map<bitset, uint8_t>` search, stops after `size_` symbols, and returns empty data when the payload runs out or 12 bits match no code;
    - `IEngine` lost `build_dictionary`/`build_tree`/`create_hash_table`, in the mock too.
  - Tree only for display, as decided: `print_graph` rebuilds a `SymbolNode` tree from the codes (`build_subtree` in `engine.cpp`, with parent links, which the pre-order iterator needs) and prints Graphviz with frequencies (0 after a decode) and codes. `print_dict` prints `code => [symbol]`, with non-printable bytes as `0xNN`. `tree/` and `SymbolNode` stay with their tests.
  - Tests:
    - `HuffmanTests` (8): empty input, single symbol, `"go go gophers"` = 37 bits, a 1/1/2/4 example with cap 3 and cap 2, Fibonacci frequencies (19 uncapped, 12 capped, valid), 256 equal frequencies → all 8, RFC 1951 codes, invalid lengths;
    - `CompressorDataTests`: golden bytes for `"aab"`, stream round trip, short header, four invalid headers;
    - `EngineTests`: a skewed round trip through a stream at the cap, a truncated payload decoding to nothing, `print_dict`, `print_graph` for two symbols and for one.

    81/81 pass; `quality.sh check` passes.
  - Benchmark against the baseline (machine heavily loaded, load average about 50, so speeds are rough):
    - header 430–2825 B → 136 B for every file;
    - small files compress much better (`grammar.lsp` 0.80 → 0.62, `xargs.1` 0.80 → 0.65, `lorem_5p.txt` 0.68 → 0.58), and `flower.png`/`lena.jpg` now grow by only 0.01%/0.05%;
    - encode is about 2.2× faster (array lookups instead of a `std::map` and `shared_ptr` per byte);
    - decode is unchanged.
  - The cap costs more than the expected 0.1% on a few large files. Measured ratios for caps 11/12/13/15 against uncapped: `reymont` 0.6218/0.6137/0.6102/0.6083 vs 0.6087 (cap 12 is 0.8% bigger), `dickens` 0.5729 vs 0.5717 (0.2%), the rest at most 0.1%. The 4-bit length field allows caps up to 15 without a format change, so the decode table step decides the final cap by measuring decode speed.
  - `ptt5`'s efficiency is 1.38: it has less than 1 bit of entropy per byte, and Huffman needs at least 1 bit per symbol. An entropy coder like ANS or arithmetic coding would fix that, not this step.
- [x] **Decode lookup table (done):** numbers in [benchmarks.md](benchmarks.md) ("Decode lookup table"), reference results in `docs/benchmarks/decode-table.json`. Decode runs at 170–340 MiB/s, up from 1.4–7.5.
  - `huffman.h`: `DecodeEntry {symbol, length}` (length 0 = no code starts here), `DecodeTable` (`std::vector`, 2^`max_code_length` entries), `build_decode_table(codes)`.
  - `bitset::data()` returns the blocks as `std::span<const uint8_t>`. It's named `data()` rather than the planned `blocks()`, which is the member's name.
  - `Engine::decode` uses a `BitReader` (anonymous namespace in `engine.cpp`): a 64-bit MSB-first buffer, refilled a byte at a time through a `constexpr` bit-reversal table, zeros past the end, and `remaining()` for the real bit count. Each symbol is one `peek()`, one table lookup and one `consume(length)`. It returns empty data on a length-0 entry or a code longer than the remaining bits. `bit_dict_` is gone; the encoder still uses `code_bits_`.
  - **Cap is now 15** (`max_code_length`), by the agreed rule: interleaved runs (median of 3, best of 2 rounds) gave 15/12 = 1.01× decode speed (geometric mean over Canterbury and Silesia); files of a few KB lose up to 10% from filling the 64 KB table. The ratio is now equal to or better than the baseline everywhere. A 4-bit length field can't exceed 15, so `CompressorDataTests.Invalid_headers_fail`'s "length above the cap" case became "Kraft sum below 1".
  - `tools/bench.sh` lost `--all`; Silesia is in the default run (about 2 minutes in total).
  - Tests:
    - `BitsetTests.Data_holds_the_first_bit_in_the_least_significant_bit`;
    - `HuffmanTests.Decode_table_maps_every_bit_pattern_to_the_code_it_starts_with` (RFC 1951 example, every entry) and `Decode_table_marks_patterns_that_start_no_code`;
    - `EngineTests.A_bit_pattern_that_starts_no_code_decodes_to_nothing` and `A_code_that_ends_past_the_payload_decodes_to_nothing`.

    86/86 pass; `quality.sh check` passes; `compressor_app` round-trips `grammar.lsp` and `mozilla`.
  - Found: the CLI decodes `mozilla` in 1.9 s against 0.2 s in memory. `operator>>`'s bit-by-bit payload conversion and the byte-wise stream reads now dominate; the "Copies and I/O" step handles them.
- [x] **Encode bit buffer (done):** numbers in [benchmarks.md](benchmarks.md) ("Encode bit buffer"), reference results in `docs/benchmarks/encode-bit-buffer.json`. Encode runs at 210–960 MiB/s, up from 25–138 (2.5–38×), faster than zlib's Huffman-only mode on every file; decode is unchanged; the encoded bytes are identical to before.
  - `EncodedData` is `{size_, code_lengths_, payload_}`; `payload_` is a `std::vector<uint8_t>`, MSB-first and zero-padded to a byte, exactly the file's payload. As decided, there's no bit count in memory, since the file doesn't keep one either; a stream round trip now compares fully equal. `operator<<`/`>>` write and read the bytes directly.
  - `engine.cpp` (anonymous namespace): `BitWriter`, a 64-bit buffer that writes 32 bits at a time (at most 46 bits buffered with 15-bit codes) and pads the last byte in `flush()`. `Engine::encode` sizes the payload once from frequencies × code lengths and puts `codes_[symbol]` directly. `BitReader` reads the bytes as they are (no bit-reversal table) and counts `remaining()` in whole bytes. `code_bits_` and `to_bitset` are gone.
  - **`bitset` deleted**, as decided: `bitset/`, its 20 tests, its benchmarks, and also `utils/` (bitset's helpers, 2 tests). `boost-dynamic-bitset` and all boost pins are gone from `vcpkg.json` and the root `CMakeLists.txt`. Tree node tags are now a `std::string` (`BinaryNode::setTag`/`tag()`), used only by `print_graph`. `bitset.h` had provided `<iostream>`, so `data.h` (`<iosfwd>`), `data.cpp`, `reader.h` and `writer.h` (`<fstream>`, `<string>`) now include what they use.
  - **clang-tidy:** the four checks left out until Phase 5 are on again. Fixed: signed bitwise in a test helper (`data.cpp`), `ProfileResult::name` no longer `const` (so its move can't throw), `try`/`catch` around both example `main`s; `BinaryNodePreOrderIterator::operator++` shadows the base on purpose and carries a `NOLINTBEGIN/END`.
  - Tests: `EngineTests.Encode_packs_codes_first_bit_most_significant` (`"aab"` → `0x20`) and `Encode_packs_codes_across_bytes_and_pads_the_last_one` (36 bits → `4D 34 D3 4D 30`); the skewed round trip checks that the stream read-back equals the encoded data; `A_code_that_ends_past_the_payload_decodes_to_nothing` now uses a whole byte (`0x01`, size 8); the byte payloads replace `bitset`s in the other tests.

    66/66 pass (86 − 22 deleted + 2) in `debug` and `release`; `quality.sh check` passes; `compressor_app` round-trips `grammar.lsp`, `mozilla` and `flower.png`.
  - CLI on `mozilla`: encode 0.3 s, decode 0.85 s (was 1.9 s). The rest is the byte-wise stream I/O for the next step.
- [x] **Copies and I/O (done):** numbers in [benchmarks.md](benchmarks.md) ("Copies and I/O"), reference results in `docs/benchmarks/copies-and-io.json`. CLI on `mozilla`: decode 0.80 → 0.22 s, encode 0.26 → 0.15 s; peak RSS 93 MB (input + output). The file format and encoded bytes are unchanged.
  - `data.cpp` (anonymous namespace): `read_rest` reserves the rest of a seekable stream, then reads in 64 KiB `sgetn` blocks, so pipes still work; it checks for EOF before growing an exactly reserved buffer. `write_bytes` writes a buffer with one `write`. Both `operator>>` and `operator<<` use them.
  - `Engine::encode`/`decode` already took `const&` and `Compressor` moves the results, so the only copy left was the test-only `DecodedData(const std::string&)`. It's gone, and `DecodedData` is a plain aggregate; the engine tests build input through a local `decoded(std::string_view)`.
  - `tools/bench-cli.sh`: hyperfine (or a python3 fallback with the same `summary.time_wall_clock` JSON) on `compressor_app encode`/`decode`, after a round-trip check; see `docs/development.md`.
  - Tests in `compressor/test/src/data/data.cpp`: `Decoded_data_round_trips_every_byte_value` (also replaces old content), `Decoded_data_reads_across_blocks` (3 MiB + 7), and `Decoded_data_reads_from_a_stream_that_cannot_seek` and `Encoded_data_reads_from_a_stream_that_cannot_seek` (a `PipeBuffer` streambuf handing out 1000 bytes per underflow).

    70/70 pass in `debug` and `release`; `quality.sh check` passes.
- [x] **Report corrupt input (done):** `compressor_app decode` prints `Cannot read <file>: the header is corrupt` or `Cannot decode <file>: the payload is corrupt` and exits 1; the output file isn't created or changed. The format rules are in [file_format.md](file_format.md) ("Decoding").
  - `compressor/include/compressor/error.h`: `enum class Error : std::uint8_t { read_failed, corrupt_header, corrupt_payload }` and `describe(Error)` (in `error.cpp`), meant to be reused by Phase 6.
  - `IDataReader<T>::read()` returns `std::expected<T, Error>`: `badbit` gives `read_failed`, `failbit` (set only by `operator>>` on an invalid `EncodedData` header) gives `corrupt_header`. `IEngine::decode` returns `std::expected<DecodedData, Error>`. `Compressor::encode`/`decode` stop before opening the writer on either error. The mocks and the codec benchmark follow.
  - Stricter than planned at first, as decided: `Engine::decode` also rejects bytes after the one holding the last code, non-zero padding bits, and a payload for a decoded size of 0.
  - Tests: `EngineTests.A_trailing_byte_after_the_codes_is_corrupt`, `Non_zero_padding_is_corrupt`, `A_payload_for_empty_data_is_corrupt`, `Codes_that_fill_the_last_byte_exactly_decode` (the three earlier `..._decodes_to_nothing` tests are now `..._is_corrupt`); `CompressorDataTests.Reader_*` (four, through real files); `CompressorTests.Encoding_fails_without_writing_when_the_input_cannot_be_read`, `Decoding_fails_without_writing_when_the_header_is_corrupt`/`_payload_is_corrupt`; `EdgeCaseTests.Decoding_a_truncated_header_fails_and_creates_no_output`, `Decoding_a_truncated_payload_fails_and_leaves_the_output_untouched`, `Decoding_with_bytes_appended_fails`. The round-trip helpers now return and assert the `bool`.

    84/84 pass in `debug` and `release`; `quality.sh check` passes. The same three cases checked by hand with `compressor_app`.
- [x] **Stored fallback (done):** the encoded file starts with a method byte, and the encoder stores the input as is when that gives the smaller file. Format in [file_format.md](file_format.md), numbers in [benchmarks.md](benchmarks.md) ("Stored fallback"), reference results in `docs/benchmarks/stored-fallback.json`. `flower.png` is now 734,569 B (737,072 at the Phase 5 baseline).
  - Decided 2026-11-19: an explicit method byte at offset 0 (`0` stored, `1` Huffman, `2`–`255` reserved for later entropy coders such as rANS and rejected as `corrupt_header`), breaking the format, rather than overloading all-zero code lengths or a bit of the size field. Stored files skip the 128-byte length table: header 9 B, Huffman 137 B. Transforms and the pipeline ID are left to the container format.
  - `data.h`: `enum class Method : uint8_t { stored, huffman }`, `EncodedData::method_`, and `stored_header_bytes`/`huffman_header_bytes` (`static_assert`ed against the field sizes in `data.cpp`).
  - `Engine::encode` computes the Huffman bit count before writing any code and stores when `9 + size <= 137 + payload bytes`; ties go to stored, which decodes as one copy. So every input up to 147 bytes, empty input included, is stored. `codes_` is cleared, so `print_dict`/`print_graph` print nothing for a stored file. `Engine::decode` returns `corrupt_payload` when a stored payload's length differs from the size. No coder interface yet: stored is a branch in `Engine`, to be pulled out when a second real coder arrives.
  - Tests: `CompressorDataTests` golden bytes for both methods, `Stored_data_round_trips_through_a_stream`, `A_short_stored_header_fails`, an unknown method in `Invalid_headers_fail`; `EngineTests.Input_that_huffman_cannot_shrink_is_stored`, `Short_input_is_stored_because_the_huffman_header_costs_more`, `Empty_input_is_stored_and_round_trips`, `A_tie_is_stored_and_one_byte_saved_is_huffman` (147 vs 148 bytes), `Print_dict_and_print_graph_are_empty_after_storing`, `A_stored_payload_of_the_wrong_size_is_corrupt`; `EdgeCaseTests.Incompressible_input_is_stored_with_a_nine_byte_header` (`flower.png`). Huffman tests that used tiny inputs now use a few hundred bytes (`aab` × 201 and similar), and the corrupt-file edge cases use `abracadabra` × 50.

    94/94 pass in `debug` and `release`; `quality.sh check` passes.

After Phase 5, continue with the features in their suggested order, starting with the container format.

### Phase 6: Freestanding core (postponed 2026-11-18)

The goal is a core that could run in a kernel or an experimental OS (see "Compressed file system" and "Compressed memory" under Features). Phase 5's data structures and canonical format do most of the groundwork.

- [ ] **Freestanding core:** no iostreams, exceptions or RTTI, and no memory allocation while compressing; the caller owns both buffers. Errors use `std::expected` (never `.value()`, which throws). API:

  ```cpp
  std::expected<size_t, Error> compress(std::span<const std::byte> in, std::span<std::byte> out);
  std::expected<size_t, Error> decompress(std::span<const std::byte> in, std::span<std::byte> out);
  size_t max_compressed_size(size_t input_size);   // header + input_size: Huffman never beats a fixed 8-bit code by losing
  ```

  The header holds the original size, so `decompress` can check `out`; every read is bounds-checked against `in`.
- [ ] **Layers:** file handling and the CLI move into separate layers on top of the core. CI builds the core with `-fno-exceptions -fno-rtti`, and a test that replaces `operator new` checks that compress and decompress don't allocate.

## Known issues

Each issue names the phase that handles it.

### Correctness

- [x] **Output file must already exist** (Phase 2). `DataWriter::open` ([writer.h](../compressor/include/compressor/data/writer.h)) opens its `std::ofstream` with `std::ios::in | std::ios::binary`. Because of `in`, the open fails when the output file doesn't exist, and an existing file isn't truncated, so leftover bytes from a longer earlier file stay at the end.
- [x] **Open failures are ignored** (Phase 2). `Compressor::encode/decode` ([compressor.cpp](../compressor/src/compressor/compressor.cpp)) ignore the `bool` that `open()` returns and go on to read from or write to a stream that failed to open.
- [x] **Corrupt encoded input isn't reported** (Phase 5 "Report corrupt input"). Corrupt input decoded to an empty file with exit code 0; now it fails with a message and a non-zero exit code. The decoder fuzzing feature would test it further.
- [x] **Empty input is undefined behaviour** (Phase 2). If the input is empty, `Engine::build_tree` ([engine.cpp](../compressor/src/compressor/engine/engine.cpp)) calls `_nodes.front()` on an empty vector.
- [x] **Input with only one distinct byte is lost** (Phase 2). For input like `aaaa`, the root is the only node and its code is an empty bitset. The encoded payload is 0 bits and decoding returns nothing.
- [x] **`Engine::find_path` returns wrong codes** (Phase 2, delete). Inside the loop it checks `node` instead of `innerNode`, so every step pushes the same bit. Nothing calls it, but it is part of `IEngine`.
- [x] **`Engine` state isn't reset** (Phase 2). `_symbols_dict` and the byte/bit dictionaries carry over between `encode()` calls. Each task currently creates its own `Engine`, which hides this.
- [x] **Argparser can read past the end of `argv`** (Phase 4, replaced; covered by `CLITests.Option_without_a_value_at_the_end_fails`). In `Argparser::parse` ([argparser.cpp](../argparser/src/argparser/argparser.cpp)), a value argument given as the last token (`compressor_app encode --input`) makes `inputs_[++i]` index out of range.
- [x] **Numeric parsing is lossy** (Phase 4, replaced). `ValueParser<uint32_t/uint64_t>` ([valueparser.cpp](../argparser/src/argparser/valueparser.cpp)) uses `std::atoi`, which overflows above `INT_MAX` and quietly turns invalid input into 0.
- [x] **The CLI does nothing silently** (Phase 2). If neither `encode` nor `decode` is given, `CLI::run` exits with code 0 and prints no error or help.

### File format

- [x] **Files aren't portable** (Phase 5 canonical step; magic number and version still come with the container format). The dictionary size and every `bitset` length were written as raw `size_t` in native byte order. Now the size is little-endian and the lengths are nibbles ([file_format.md](file_format.md)).
- [x] **No spec** (container format step 1). [file_format.md](file_format.md) specifies the ingot container; [compression.md](compression.md) still contains only a heading.

### Performance and limitations

- [x] **Slow decoding** (Phase 5 decode table). Each bit was pushed into a `bitset`, followed by a `std::map<bitset, uint8_t>` lookup; now one table lookup per symbol.
- [ ] **Single-threaded** (container step "multithreaded blocks"). The in-memory part is fixed: container step 2 streams one block at a time.
- [x] **CRC32 caps stored blocks at about 1.9 GB/s (done)** (found in container step 2). `compressor/checksum/crc32.{h,cpp}` has a hardware path: ARMv8 `__crc32d`/`__crc32b`, and x86 PCLMULQDQ folding (4×128-bit lanes, Barrett reduction, Intel/zlib constants). `crc32_portable()` keeps slicing-by-8 as the fallback for Phase 6, and `crc32_hardware()` says which one `crc32()` uses. `Crc32Tests` compare both against zlib (lengths 0–300, 1 MB, unaligned starts, continuation) and check that Apple arm64 uses the hardware path. Results (`docs/benchmarks.md`, "Hardware CRC32"): stored decode about 2,100 → 8,000 MiB/s, Huffman decode +5–20%, encode +10–30%.
- [x] **Hardware CRC32 isn't on by default everywhere (done).** Generic x86-64 and Linux aarch64 builds don't define `__PCLMUL__`/`__ARM_FEATURE_CRC32`. Instead of a CMake flag, which would make the binary need those features, the hardware functions carry `__attribute__((target(...)))` (`"crc"` for clang, `"+crc"` for GCC, `"pclmul,sse4.1"` on x86), and `crc32_hardware()` checks the CPU once (a function-local static): `getauxval(AT_HWCAP) & HWCAP_CRC32` on Linux, `sysctlbyname("hw.optional.armv8_crc32")` on macOS, `__builtin_cpu_supports` on x86 GCC/clang, `__cpuid` on MSVC x64. When the compiler already targets the feature, the check is a constant `true`. No measurable cost on the benchmarks.
  - Checked with Apple clang: arm64 default and `-march=armv8-a` (runtime path), x86_64 without flags and with `-mpclmul -msse4.1` under Rosetta, each against the portable CRC for lengths 0–300 and 100 KB. Not compiled yet: GCC (Homebrew's GCC 14 can't build against the current macOS SDK), Linux (`getauxval`) and MSVC; the Lima VM `default` or CI can cover them. MSVC ARM64 and other platforms fall back to the portable CRC.
- [ ] **The ARM CRC32 loop is one `crc32x` chain**, latency-bound at about 8 GB/s on an M3 Max. Three interleaved chains merged with PMULL would roughly triple it, if stored blocks ever matter that much.

### Build and tooling

- [x] **Wrong script name in the docs** (Phase 2). [development.md](development.md) says `./configure.sh`; the script is `./generate.sh`.
- [x] **Bitset benchmarks truncate their size** (Phase 5 benchmark harness). `compressor_benchmark/src/bitset/*.cpp` store `state.range(0)` (up to 1024) in a `uint8_t`, so the larger runs measure sizes modulo 256.
- [x] **Benchmark builds aren't reproducible** (Phase 3, fixed by vcpkg's pinned baseline). google benchmark is downloaded from `master.zip`, not a pinned tag.
- [x] **Dependencies are downloaded during configure** (Phase 3, now vcpkg ports, installed by the manifest). googletest and benchmark come from ExternalProject; Boost (1.81 or later) must be installed separately (`brew install boost`).
- [x] **`Testing/` isn't gitignored** (Phase 2).
- [x] **Global compiler flags** (Phase 3). `-Wall -Wextra -Werror` go into the global `CMAKE_CXX_FLAGS`, so `-Werror` also applies to googletest and benchmark. `-O3` is added globally, even to Debug builds, when ASan is off.
- [x] **Hard-coded Xcode architecture** (Phase 3). With the Xcode generator, `CMAKE_OSX_ARCHITECTURES` is forced to `x86_64`, so Apple Silicon Macs build Intel code that runs under Rosetta.

## Features

Ideas for after the basics are done. Each section is roughly in order of difficulty.

### Container format and streaming

The foundation for most features below, so design it first. Plan approved 2026-11-19.

Format version 1, all integers little-endian:

- **Frame header (8 bytes):** 4-byte magic `89 49 4E 47` (`0x89` then `ING`, for the project's new name, ingot; extension `.ingot`), format version `1`, log2 of the block size (default 20 = 1 MiB, accepted range 12–26, `--block-size` overrides it), 2 flag bytes that must be 0.
- **Block, repeated:** method (0 stored, 1 Huffman), decoded size (u32, 1..block size), encoded size (u32, bytes after this 13-byte block header, so blocks can be skipped or read in parallel), CRC32 of the decoded bytes (u32), then the 128 code-length bytes (Huffman only) and the payload.
- **End block:** method `0xFF`, then the total decoded size (u64), which catches missing blocks. Every block but the last holds exactly the block size.

Decisions: the checksum is CRC32 (IEEE), our own table-driven implementation in `compressor/`, tested against zlib's `crc32`. gzip and zip need the same CRC, and it keeps the core dependency-free for Phase 6. Each block carries its own CRC so a single block can be verified on its own. The stored fallback works per block. Out of scope: a block index (comes with folders and search) and pipelines (the method byte leaves room).

- [x] **Step 1, frame and CRC32 (done):** blocks came in this step already, still in memory, so no single-block interim format was needed.
  - `compressor/checksum/crc32.{h,cpp}`: `crc32(bytes, crc)`, slicing-by-8 with tables built at compile time; `Crc32Tests` check the standard value and compare against zlib (`ZLIB::ZLIB` is linked into `compressor_test` only).
  - `data.h`: `EncodedBlock` (method, size, CRC, code lengths, payload) and `EncodedData` (block size log2 plus blocks). `operator<<` writes the container; `read_encoded()` parses it and returns a specific `Error`, and `operator>>` wraps it. `DataReader<EncodedData>` calls `read_encoded()`. New errors: `not_ingot`, `unsupported_version`, `truncated`, `checksum_mismatch`.
  - `Engine(block_size_log2)`: `encode`/`decode` loop over the public `encode_block`/`decode_block`, which choose stored or Huffman per block and check the CRC. They aren't in `IEngine` yet; step 2's streaming coder will need them there.
  - Tests: `CompressorDataTests` (byte layout, every truncation, each header rule), `EngineTests` (block splitting at B−1, B, B+1, 3B; per-block method; CRC failures), `EdgeCaseTests` (framing overhead, a changed byte fails with no output). 112/112 pass.
  - Not measured yet: the CRC's cost on encode and decode speed. Step 2 re-runs the benchmarks.
- [x] **Step 2, block streaming (done):** encode and decode hold one block at a time, so memory no longer depends on the file size (5 MiB peak on a 51 MB file, was 89 MiB).
  - `data.h`: the container is written piece by piece with `write_frame_header`/`write_block`/`write_end_block`, and read by `FrameReader` (`read_header()`, then `read_block(block)` until it returns false, reusing the block's payload capacity). It checks every format rule, including the end block's total and trailing bytes, but not CRCs. `operator<<` and `read_encoded()` are built on them.
  - `IEngine` now has `encode_block`/`decode_block`; the whole-buffer `encode`/`decode` stay on `Engine` only, for tests and benchmarks.
  - `stream/encoder.h`, `stream/decoder.h`: `IBlockEncoder`/`IBlockDecoder::encode/decode(std::istream&, std::ostream&)` returning `std::expected<void, Error>`; `BlockEncoder(std::unique_ptr<IEngine>, block_size_log2)` and `BlockDecoder(std::unique_ptr<IEngine>)`. The encoder stops after a short read, so it won't block on a pipe. The decoder writes each block once its CRC matches, so on failure its stream may hold earlier blocks. New error `write_failed`. Mocks in `test/mock/stream/`.
  - `ICompressor::encode/decode(const fs::path& input, const fs::path& output)`; `Compressor(encoder, decoder, err = std::cerr)`. A regular or missing output is written to `.<name>.<random>.tmp` next to it (opened with `std::ios::noreplace`), then renamed over it on success; on failure the temp file is removed. So a failed encode or decode leaves the output untouched, and encoding a file onto itself works. Outputs that aren't regular files (`/dev/null`, pipes) are written in place.
  - Deleted: `IDataReader`/`IDataWriter`, `DataReader`/`DataWriter`, the `task/` package (`ICoderTask`, `EncoderTask`, `DecoderTask`) and their mocks.
  - Tests: `BlockStreamSizeTests` (round trip at 0, 1, B−1, B, B+1 and 3B with B = 4 KiB, and the stream output equals the in-memory `operator<<` output), `BlockStreamTests` (pipe input for both coders, one engine call per block, stopping at the first failed block, earlier blocks already written, container errors, `read_failed`, `write_failed`), `CompressorTests` rewritten over mock coders and real temp files (replace, onto itself, missing input or directory, failure leaves no temp file, `/dev/null`), and two `FrameReader` tests. 125/125 pass.
  - Benchmarks (`docs/benchmarks.md`, "Block streaming"): CRC32 costs 10–15% on Huffman blocks and caps stored blocks at about 1.9 GB/s. CLI decode is 11% slower only because a new file plus deleting the old one costs more than truncating in place.
  - Not done: the rename doesn't keep an existing output's permissions or replace a symlink's target (it replaces the link). Hardware CRC32 is a known issue under "Performance and limitations".
- [x] **Step 3, stdin/stdout (done):** `-i`/`-o` accept `-` or may be omitted, so the tool works in shell pipelines (`tar c dir | compressor_app encode > x`).
  - `compressor.h`: `standard_stream` (`"-"`) as an input or output path means stdin or stdout (`./-` names a file called `-`). `StandardStreams{in, out, err}` replaces the `err` constructor argument; `process_streams()` is the default. The standard output is written in place and flushed, a failed flush is `write_failed`, and a failed decode leaves the earlier blocks there. Errors name "standard input".
  - `stream/stdio.{h,cpp}`: `StdioInputBuf`/`StdioOutputBuf`, `std::streambuf`s over a `FILE*` with a 64 KiB buffer and direct `fread`/`fwrite` for larger pieces; a read error throws, which `std::istream` turns into badbit. Found along the way: libc++'s `std::cin`/`std::cout` go through stdio one character at a time and ignore `sync_with_stdio(false)`, so a pipe was 5x slower than a file. Now a 35 MB release run is 0.065 s encode and 0.145 s decode through pipes, against 0.10 s and 0.18 s between files.
  - CLI: an omitted `-i`/`-o` defaults to `-`; `-i` still checks that a file exists unless it is `-`. `main.cpp` switches stdin/stdout to binary mode on Windows (untested there).
  - Tests: `CompressorTests` (stdin to stdout, stdin to a replaced file, a file to stdout, a failed decode keeps the partial stdout, a failing stdout is a write error), `StdioBufTests` (pieces below, at and above the buffer size, `peek`/`get` mixed with `read`, empty input, flush on destruction, read and write errors), `CLITests` (omitted paths, `-` for each, the error names standard input), and `PipeRoundTrip.*` CTests that run `compressor_app` through `compressor_app/test/pipe_roundtrip.cmake`. 142/142 pass.
  - Not done: encoding to a terminal isn't refused (gzip refuses without `-f`), so `compressor_app encode -i file` with no `-o` prints binary to the terminal.
- [ ] **Later, multithreaded blocks:** encode several blocks in parallel through a bounded queue, keeping the output order deterministic; benchmark it. Not part of this feature.
- [ ] **Later, rename to ingot:** the targets (`compressor_app` → `ingot`), the CLI name and help text, the README and the docs. The magic and `.ingot` already use the name.

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

1. Container format with streaming and blocks (steps 1–3, done).
2. Hardware CRC32 (known issue "CRC32 caps stored blocks", done).
3. Multithreaded blocks (container format, "Later").
4. Folder support and the extra commands.
5. An LZ77 pipeline next to Huffman.
6. Deflate output for zip/gzip compatibility.
7. Searching through the block index.
8. Read-only compressed file system image, and the compressed-memory experiments.
9. Advanced options: FM-index, FUSE mount, type-specific transforms.

The benchmark harness and fuzzing can be added alongside any of these steps.
