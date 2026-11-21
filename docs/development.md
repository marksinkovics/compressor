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
| `python` | Ninja | Release with `COMPRESSOR_PYTHON=ON`: also the Python module (see "Python module") |
| `fuzz` | Ninja | RelWithDebInfo with `COMPRESSOR_FUZZ=ON`, built by Homebrew's clang (`$HOMEBREW_PREFIX/opt/llvm/bin/clang++`; macOS only). See "Fuzzing". |

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
| `COMPRESSOR_PYTHON` | `OFF` | Builds `python/`, the `ingot` Python module, through the manifest feature `python` (pybind11, which needs vcpkg's `python3` port: the first configure builds CPython and its libraries, about 10 minutes). Static libraries are built position-independent so they can go into the module. |
| `COMPRESSOR_METAL` | `ON` on macOS with the Metal toolchain, else `OFF` | The GPU paths (`gpu/`): `kernels.metal` is compiled with `xcrun metal` into a metallib that `cmake/EmbedFile.cmake` embeds in the library, and `gpu.mm` (Objective-C++) runs it through Metal. Every GPU path has a CPU one; set `COMPRESSOR_NO_GPU=1` at run time to force the CPU. |
| `COMPRESSOR_FUZZ` | `OFF` | Coverage instrumentation and ASan for our code, and libFuzzer for the `fuzz/` targets. Needs a clang that ships libFuzzer (Apple Clang doesn't); vcpkg's ports stay uninstrumented. |

Every target gets `-Wall -Wextra -Wpedantic` (`/W4` on MSVC) from the root `add_compile_options`, minus `-Wmissing-designated-field-initializers` where the compiler has it (clang 23 puts it in `-Wextra`; designated initializers that leave fields at their defaults are intended); every target built in this tree is ours, and vcpkg headers are included as system headers. Optimization comes from the build type only (Release: `-O3 -DNDEBUG`). Tests are discovered when `ctest` runs (`DISCOVERY_MODE PRE_TEST`), not at build time.

### Python module

```bash
cmake --workflow --preset python      # build/python/python/package/ingot.cpython-312-darwin.so
PY=build/python/vcpkg_installed/arm64-osx/tools/python3/python3.12
PYTHONPATH=build/python/python/package $PY -c 'import ingot; print(ingot.decompress(ingot.compress(b"x" * 100)))'
```

```python
import ingot
packed = ingot.compress(data, level=9, threads=0, dictionary=None)   # bytes -> ingot container
data = ingot.decompress(packed, threads=0, dictionary=None)          # not folder archives
ingot.compress_file("photos", "photos.ingot", level=6)               # files and folders, as the CLI
ingot.decompress_file("photos.ingot", "photos-copy")
dictionary = ingot.train_dictionary([sample1, sample2, ...], size=112640)  # a dictionary file's bytes

c = ingot.Compressor(level=6, dictionary=None)     # like zlib.compressobj(): whole blocks as they fill
stream = c.compress(part1) + c.compress(part2) + c.flush()
d = ingot.Decompressor(dictionary=None)            # like zlib.decompressobj(): d.eof, d.unused_data
data = d.decompress(stream[:1000]) + d.decompress(stream[1000:]) + d.flush()

ingot.list("photos.ingot")                         # [{"path", "type", "size", "mode", "mtime_ns", ...}]
ingot.read("photos.ingot", "2024/beach.bmp")       # one file's bytes, decoding only its blocks
ingot.extract("photos.ingot", "2024", "out-2024")  # a file, directory or symlink
# failures raise ingot.Error
```

Every build also writes a wheel, `build/python/python/dist/ingot-<version>-cp312-cp312-<platform>.whl` (`tools/python_wheel.py`, standard library only; on macOS the platform tag is the module's minimum OS, `macosx_26_0_arm64` here), which `pip install` puts into any CPython 3.12 of that platform, pyenv's and Homebrew's included. ctest's `Python.Wheel` installs it into a fresh venv and imports it. The streaming objects work on one thread and give the same bytes as `compress()` without a dictionary; a single-block frame (`compress()` with a dictionary) comes out of a `Decompressor` only at `flush()`, since its payload runs to the end.

The module is built against vcpkg's CPython (3.12), so it imports only into a Python 3.12, such as vcpkg's own interpreter above. The GIL is released while coding. ctest's `Python.Module` runs `python/test/test_ingot.py` (plain asserts, no pytest), `Python.Wheel` `python/test/install_wheel.py`. clang-tidy in `tools/quality.sh` reads the debug build's compile commands, which don't have the module; check it with `$(brew --prefix llvm)/bin/clang-tidy -p build/python python/src/ingot.cpp`.

### WebAssembly demo

`web/` is a static page that shows ingot's Huffman code for typed text (counts, the code tree, the bit stream, the codes) and the container size at every level, from the engine compiled to WebAssembly (`web/src/demo.cpp`, embind: `analyze(text)` returns JSON, `decode(bytes)` decodes an ingot file). Emscripten comes from emsdk inside the git-ignored `build/` folder, so nothing is installed system-wide:

```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git build/emsdk
build/emsdk/emsdk install 7.0.0 && build/emsdk/emsdk activate 7.0.0
bash tools/web.sh serve    # builds build/web/ and serves it on http://localhost:8000
bash tools/web.sh test     # web/test/test_demo.mjs under emsdk's node
```

`tools/web.sh` compiles the engine's sources with `em++` straight from their list in the script (no CMake, no threads, no file I/O), and only when a source is newer than `build/web/ingot_demo.wasm`. ctest runs it as `Web.Demo` when `build/emsdk` exists. Besides the page's own checks, the test decodes every fuzz seed a native build wrote (`fuzz/corpus/container/`), so a 32-bit build must run the same models as a 64-bit one; building for wasm found one place where it didn't (a `size_t` shifted by 56 in the CM coder's contexts) and code only the ARM CRC32 path uses.

## Run tests

Tools

* [Google test framework](https://github.com/google/googletest)
* [CTest](https://cmake.org/cmake/help/latest/module/CTest.html)

Run `cmake --workflow --preset debug`, or `ctest --preset debug` after a build. A single binary runs directly too, for example `build/debug/compressor/test/compressor_test_app --gtest_filter='EngineTests.*'`.

The round-trip tests (`RoundTripTests`) encode and decode every file in `compressor_example/samples/raw/` and compare the result byte for byte; a file added there is tested automatically.

`PipeRoundTrip.*` (in `ingot/CMakeLists.txt`) run the built `ingot` through `ingot/test/pipe_roundtrip.cmake`: stdin-to-stdout encoding must match file encoding, and `encode | decode` must give the input back. They cover an empty input, `lorem_50p.txt` and `flower.png`.

ThreadSanitizer has no preset yet; an ad hoc build in `build/tsan` checks the thread pool and the block coders (gtest stays uninstrumented, which hasn't caused reports so far). It runs inside Claude's sandbox, unlike ASan:

```bash
cmake --preset debug -B build/tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build/tsan --target compressor_test_app
TSAN_OPTIONS=halt_on_error=1 build/tsan/compressor/test/compressor_test_app \
  --gtest_filter='ThreadPool*:*BlockStreamThread*:BlockStreamTests.*' --gtest_repeat=30
```

Linux code paths (GCC, `getauxval`) can be checked on a Mac with Apple's `container` (`brew install container`, then `container system start`); add `--arch amd64` for x86-64 under Rosetta. The full build also needs CMake, Ninja and vcpkg in the image, so a quick check compiles single files:

```bash
container run --rm -v "$PWD":/src -w /src docker.io/library/gcc:14 \
  g++ -std=c++23 -Icompressor/include -c compressor/src/compressor/checksum/crc32.cpp
```

### Fuzzing

`fuzz/` has one libFuzzer target per kind of untrusted input: `fuzz_container` (`decode_frame` with and without the archive flag, `read_encoded`, decoding with the fixed dictionary in `fuzz/src/dictionary.h`, which the coder 9 and 10 seeds are made with), `fuzz_archive` (`read_archive_index`, then a check that every accepted path stays inside the extraction directory, `list_archive`, `verify_archive`, `extract_file` of every file, `select_subtree` of every other entry) `fuzz_stats` (`collect_stats`, the report and the graph) and `fuzz_image` (`decode_image`, and re-encoding what decodes; `decode_lossy_image`; `read_pnm`; headers claiming more than 4 megapixels are skipped to stay within libFuzzer's memory limit). Nothing is written to disk: a path bug found by extracting for real could write anywhere.

- In normal builds each target links `fuzz/src/replay_main.cpp` instead and replays its corpus in `fuzz/corpus/<target>/` once (`FuzzCorpus.*` tests). A crashing input found by fuzzing goes into that directory with the fix, so it stays a regression test.
- `cmake --workflow --preset fuzz` builds with libFuzzer and ASan and runs every test, plus 50 000 runs per target (`Fuzz.*`). New corpus entries go to `build/fuzz/fuzz/corpus/<target>/`.
- A longer run: `build/fuzz/fuzz/fuzz_archive -max_total_time=600 -max_len=65536 -artifact_prefix=build/fuzz/artifacts/archive- build/fuzz/fuzz/corpus/archive fuzz/corpus/archive`.
- `fuzz_make_seeds <dir>` regenerates the seeds (4 KiB blocks, fixed times, so the output is the same every run): `build/debug/fuzz/fuzz_make_seeds fuzz/corpus`. Besides what the engine chooses, it writes one container per coder that the engine rarely picks on small inputs (`container/coder<N>.ingot`), built block by block with that coder.

Tests for known bugs are added with a `DISABLED_` prefix until the fix lands. Run one with `--gtest_also_run_disabled_tests --gtest_filter=<Suite>.DISABLED_<name>`; some of them crash or hang before their fix, so run them one at a time.

## Code quality

Formatting and linting run through `tools/quality.sh`, with one stage per tool: `clang-format`, `clang-tidy`, `shellcheck` and `shfmt`. Pre-commit hooks run every stage but clang-tidy on each commit; clang-tidy is slow, so it runs in the full `tools/quality.sh check` at the end of each step.

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
- With `-- file...`, clang-tidy checks only the sources that include those files, using `ninja -t deps`. A full run takes about 2.5 minutes.
- Settings live in `.clang-format`, `.clang-tidy` (plus a nested one in `compressor_benchmark/`), `.shellcheckrc` and `.editorconfig` (shfmt reads its options from there).

### pre-commit hooks

`.pre-commit-config.yaml` runs the generic `pre-commit-hooks` checks (whitespace, final newline, line endings, YAML/JSON, merge conflicts, large files, private keys) and one local hook per `quality.sh` stage. Each hook runs only when the commit touches its files and checks the whole tree. The clang-tidy hook is in the `manual` stage, so commits skip it; run it by hand, where it checks the sources the given files affect.

```bash
pre-commit run --all-files    # every commit hook on the whole tree (clang-tidy skipped)
pre-commit run --hook-stage manual clang-tidy --files compressor/include/compressor/huffman/huffman.h
```

### Claude Code format hook

`.claude/settings.json` registers `.claude/hooks/format-edited-file.sh` as a `PostToolUse` hook on `Edit` and `Write`. After Claude edits a C++ or shell file in the repo, the hook runs `tools/quality.sh format -- <file>`, so formatting never turns up as a lint failure. It always exits 0, so it never blocks an edit.

## Run benchmark

Tool: [Google benchmark framework](https://github.com/google/benchmark) (vcpkg port). Always benchmark the `release` preset.

```bash
bash tools/fetch-corpus.sh               # once: Canterbury and Silesia into corpus/ (git-ignored)
bash tools/fetch-corpus.sh images        # macOS only: uncompressed images made with sips
bash tools/fetch-corpus.sh pictures      # macOS + ImageMagick: the image track's PNG corpus (Kodak and drawn pictures)
python3 tools/images.py                 # lossless and lossy image coders on corpus/pictures (about 10 minutes)
bash tools/bench.sh --out new.json       # all three sets, about 2 minutes (default: build/release/benchmark.json)
bash tools/bench.sh --filter 'Engine/.*/canterbury'
python3 tools/bench-compare.py docs/benchmarks/decode-table.json new.json
bash tools/bench-cli.sh --out cli.json  # ingot end to end, file I/O included
python3 tools/compare.py                # ingot against gzip, zstd, xz, bzip2, brotli (about 10 minutes)
python3 tools/compare.py --corpus canterbury images --tools ingot xz --no-save
```

`tools/images.py` measures image coders on `corpus/pictures` (classes `photo`, `screen`, `graphic`, `alpha`): lossless as PNG at maximum effort, WebP lossless, JPEG XL lossless, `ingot image` and ingot's levels on the raw pixels, every round trip compared pixel for pixel (exit status 1 if one differs); lossy as JPEG, WebP, AVIF (ffmpeg with SVT-AV1) and JPEG XL swept over quality settings, each decoded and scored with `ssimulacra2`, with bits per pixel interpolated at the target scores (default 70, 80, 90). Times are CPU seconds, one thread per coder; `--jobs` images run at once. It writes `docs/benchmarks/images/<date>-<commit>.json` unless `--no-save`; `--classes`, `--coders` (name prefixes) and `--corpus` narrow a run. ctest's `ImagesHarness.Lossless` runs it on `ingot/test/pictures/` with ingot only. The tools come from Homebrew (`brew install imagemagick webp jpeg-xl ffmpeg`; `ssimulacra2` comes with `jpeg-xl`).

`tools/compare.py` packs each corpus into one reproducible tar (`build/compare/`) and runs `ingot -L 1/6/7/9`, `gzip -6/-9`, `zstd -3/-19`, `xz -6/-9`, `bzip2 -9` and `brotli -q 9` from the command line, checking every round trip; ingot, zstd and xz get `--threads` (default 6). It reports wall time and CPU time (user + system over all threads, which other programs' load hardly changes; `enc/cpu-s` and `dec/cpu-s` are MB per CPU second) and writes `docs/benchmarks/history/<date>-<commit>.json` (`--no-save` doesn't) and prints, for the ingot rows, the change in size and speed against the newest earlier file. Commit the file with the change it measures. `--dir PATH` adds any folder; ctest's `CompareHarness.Samples` runs it on the samples with ingot only. The tools come from Homebrew (`brew install zstd xz brotli`).

`tools/bench-cli.sh` times `ingot encode`/`decode` on `kennedy.xls`, `webster` and `mozilla` (or the files given, relative to the repo root), after checking that each round-trips. It uses [hyperfine](https://github.com/sharkdp/hyperfine) 2.x (`brew install hyperfine`) and falls back to a python3 loop without it; both write `results[].summary.time_wall_clock`, and the script prints a table from it.

How the benchmarks work, what the counters mean and the baseline numbers are in [benchmarks.md](benchmarks.md).
