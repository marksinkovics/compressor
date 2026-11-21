# Benchmarks

How to run them is in [development.md](development.md#run-benchmark). This page explains what they measure and records the baseline.

## What runs

`compressor_benchmark` (Google Benchmark, `release` preset) runs the codec benchmarks:

- **Codec benchmarks** (`compressor_benchmark/src/codec/codec.cpp`), registered at runtime for every file in three sets:
  - `samples`: `compressor_example/samples/raw/`, in the repo;
  - `canterbury`: the [Canterbury corpus](https://corpus.canterbury.ac.nz/descriptions/#cantrbry), 11 files, 2.8 MB;
  - `silesia`: the [Silesia corpus](https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia), 12 files, 211 MB.

  `tools/fetch-corpus.sh` downloads the corpora into `corpus/` (git-ignored) and checks each archive's SHA-256. A missing set is skipped with a note on `stderr`.

Codec benchmark names are `<codec>/<op>/<set>/<file>`, with non-alphanumerics in the file name replaced by `_`. For example, `Engine/Decode/silesia/dickens`.

- `Engine`: our Huffman `Engine`, in memory (no file I/O).
- `ZlibHuffman`: the reference. zlib 1.3.1 `deflate` with `Z_HUFFMAN_ONLY` (level 6, raw stream with `windowBits -15`), which is Huffman coding without LZ77. Deflate starts a new block with fresh Huffman tables every few thousand symbols, so it adapts to local statistics. That is why it beats the order-0 entropy of the whole file on mixed data such as `mozilla` or `samba`.

Each decode benchmark compares its output with the input and fails with an error if they differ.

## Counters

| Counter | Meaning |
|---|---|
| `bytes_per_second` | Original (uncompressed) bytes per second, for encode and decode alike |
| `ratio` | Compressed size ÷ original size (encode only). For `Engine`, this is the serialized `EncodedData`, header included |
| `header` | `Engine` only: serialized size minus the payload bytes (dictionary, the two length fields, padding) |
| `efficiency` | Compressed bits ÷ (n · H), where H is the file's order-0 entropy in bits per byte. 1.0 is the best a static order-0 code can do; it is left out when H is 0 |

## Baseline (2026-11-18)

Phase 5 starting point, before any optimization: `docs/benchmarks/baseline.json` from `tools/bench.sh --all` (Silesia was opt-in then) on an Apple M3 Max (release, Apple Clang 17). The machine was not idle (load average about 7), so treat differences below about 10% as noise. Speeds are in MiB/s of original data. Silesia's `Engine` runs got one iteration each; the whole run took about 3 minutes.

| Set | File | Engine ratio | Engine efficiency | Engine header (B) | Engine enc | Engine dec | zlib ratio | zlib enc | zlib dec |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| samples | flower.png | 1.003 | 1.005 | 2580 | 13.7 | 1.38 | 0.998 | 195 | 1645 |
| samples | lena.jpg | 1.027 | 1.031 | 2588 | 12.4 | 1.41 | 1.000 | 224 | 1633 |
| samples | lorem_5p.txt | 0.678 | 1.274 | 442 | 35.4 | 4.29 | 0.550 | 182 | 290 |
| samples | lorem_10p.txt | 0.611 | 1.150 | 430 | 43.0 | 4.25 | 0.542 | 197 | 307 |
| samples | lorem_20p.txt | 0.565 | 1.069 | 442 | 41.5 | 4.32 | 0.535 | 207 | 318 |
| samples | lorem_50p.txt | 0.549 | 1.035 | 454 | 39.0 | 4.17 | 0.537 | 202 | 316 |
| canterbury | alice29.txt | 0.582 | 1.019 | 799 | 32.6 | 3.38 | 0.578 | 175 | 287 |
| canterbury | asyoulik.txt | 0.611 | 1.017 | 723 | 29.2 | 3.44 | 0.608 | 178 | 288 |
| canterbury | cp.html | 0.696 | 1.065 | 920 | 30.6 | 2.89 | 0.662 | 197 | 310 |
| canterbury | fields.c | 0.716 | 1.144 | 961 | 32.4 | 3.02 | 0.635 | 199 | 321 |
| canterbury | grammar.lsp | 0.802 | 1.386 | 816 | 25.1 | 3.34 | 0.598 | 178 | 304 |
| canterbury | kennedy.xls | 0.452 | 1.012 | 2825 | 41.9 | 3.28 | 0.418 | 164 | 315 |
| canterbury | lcet10.txt | 0.589 | 1.010 | 906 | 30.0 | 3.35 | 0.585 | 169 | 292 |
| canterbury | plrabn12.txt | 0.574 | 1.013 | 889 | 29.5 | 3.44 | 0.574 | 168 | 284 |
| canterbury | ptt5 | 0.211 | 1.395 | 1755 | 58.9 | 7.49 | 0.208 | 185 | 367 |
| canterbury | sum | 0.743 | 1.115 | 2757 | 21.6 | 2.30 | 0.643 | 173 | 304 |
| canterbury | xargs.1 | 0.803 | 1.311 | 791 | 25.4 | 3.21 | 0.629 | 179 | 300 |
| silesia | dickens | 0.572 | 1.009 | 1110 | 27.7 | 3.39 | 0.572 | 163 | 281 |
| silesia | mozilla | 0.781 | 1.004 | 2752 | 25.1 | 1.95 | 0.681 | 138 | 255 |
| silesia | mr | 0.464 | 1.007 | 2811 | 32.6 | 3.08 | 0.441 | 124 | 251 |
| silesia | nci | 0.305 | 1.004 | 690 | 49.4 | 5.77 | 0.307 | 181 | 318 |
| silesia | ooffice | 0.833 | 1.004 | 2753 | 20.7 | 1.57 | 0.782 | 103 | 202 |
| silesia | osdb | 0.827 | 1.004 | 2754 | 17.4 | 1.70 | 0.832 | 137 | 245 |
| silesia | reymont | 0.609 | 1.006 | 2788 | 31.1 | 2.75 | 0.603 | 165 | 292 |
| silesia | samba | 0.766 | 1.005 | 2763 | 28.2 | 2.07 | 0.654 | 168 | 299 |
| silesia | sao | 0.944 | 1.004 | 2718 | 17.1 | 1.57 | 0.929 | 137 | 258 |
| silesia | webster | 0.625 | 1.007 | 1068 | 29.6 | 2.97 | 0.625 | 163 | 269 |
| silesia | x-ray | 0.829 | 1.004 | 2818 | 20.3 | 1.75 | 0.800 | 146 | 265 |
| silesia | xml | 0.694 | 1.007 | 1114 | 37.5 | 2.67 | 0.619 | 168 | 289 |

Takeaways:

- **Decode is the bottleneck:** 1.4–7.5 MiB/s, which is 100–1000× slower than zlib and about 10× slower than our own encoder.
- **Encode is 4–10× slower than zlib** (12–59 MiB/s).
- **On large files the code itself is near-optimal:** efficiency is 1.004–1.009, so the rest of Phase 5 can only gain ratio on small files.
- **The header costs up to 2.8 KB** (one serialized `bitset` per symbol, plus 8-byte length fields). It dominates small files: 39% over entropy on `grammar.lsp` (3.7 KB), 27% on `lorem_5p.txt`. Canonical Huffman brings it to at most a few hundred bytes.
- **Already-compressed input grows:** `flower.png` by 0.3%, `lena.jpg` by 2.7%. This needs the stored fallback.

## Canonical Huffman (2026-11-19)

After the "Canonical Huffman with length-limited codes" step, diffed with `tools/bench-compare.py` against the baseline. The machine was heavily loaded (load average about 50), so the speeds are rough; the ratios are exact. The JSON wasn't kept: the next step diffs against the baseline again.

- **Header:** a fixed 136 B instead of 430–2825 B.
- **Ratio on small files:** much better. `grammar.lsp` 0.802 → 0.620, `xargs.1` 0.803 → 0.648, `fields.c` 0.716 → 0.642, `lorem_5p.txt` 0.678 → 0.580. `flower.png` and `lena.jpg` now grow by only 0.01% and 0.05%.
- **Ratio on large files:** the same within 0.1%, except where the 12-bit cap binds: `reymont` 0.6087 → 0.6137 (+0.8%), `dickens` 0.5717 → 0.5729 (+0.2%).
- **Encode:** about 2.2× faster (e.g. `dickens` 28 → 64 MiB/s), from array lookups instead of a `std::map` and `shared_ptr` per byte.
- **Decode:** unchanged (still bit by bit).

Ratio by cap (encode only, same build otherwise):

| File | Uncapped (baseline) | 11 | 12 | 13 | 15 |
|---|---:|---:|---:|---:|---:|
| silesia/reymont | 0.6087 | 0.6218 | 0.6137 | 0.6102 | 0.6083 |
| silesia/dickens | 0.5717 | 0.5747 | 0.5729 | 0.5721 | 0.5717 |
| silesia/webster | 0.6254 | 0.6270 | 0.6260 | 0.6256 | 0.6255 |
| silesia/nci | 0.3047 | 0.3058 | 0.3052 | 0.3049 | 0.3047 |
| canterbury/plrabn12.txt | 0.5738 | 0.5741 | 0.5730 | 0.5725 | 0.5722 |

The other files differ by at most 0.1% between caps. Up to 15 fits the format's 4-bit length field, so the decode-table step picks the cap from decode speed.

## Decode lookup table (2026-11-19)

After the "Decode lookup table" step, which also raised the cap to 15 bits. Results are in `docs/benchmarks/decode-table.json`; compare later steps against that file. Load average was about 18, so expect ±10% noise on speeds.

- **Decode:** 170–340 MiB/s, up from 1.4–7.5: 42–220× faster on every file. On the Silesia text files that matches zlib's Huffman-only decoder (250–300 MiB/s); zlib is still 5× faster on the incompressible images, where it stores blocks raw.
- **Encode:** unchanged since the canonical step (about 2× the baseline).
- **Ratio:** with the 15-bit cap, equal to or better than the baseline on every file (`reymont` 0.6083 vs 0.6087, `dickens` 0.5717 for both).
- **Runtime:** a full `tools/bench.sh` run takes about 2 minutes, so Silesia is in the default run.

Decode speed by cap, as the median of 3 repetitions and the best of 2 interleaved rounds (MiB/s):

| File | 12 bits (8 KB table) | 15 bits (64 KB table) | 15/12 |
|---|---:|---:|---:|
| canterbury/alice29.txt | 247 | 266 | 1.08 |
| canterbury/grammar.lsp | 208 | 188 | 0.90 |
| canterbury/xargs.1 | 221 | 209 | 0.95 |
| silesia/dickens | 254 | 256 | 1.01 |
| silesia/mozilla | 247 | 263 | 1.07 |
| silesia/reymont | 245 | 256 | 1.04 |
| silesia/webster | 243 | 249 | 1.02 |

The geometric mean over Canterbury and Silesia is 1.01. Only the files of a few KB lose up to 10%, from filling the larger table, so the cap is 15.

The CLI is now limited by I/O, not by the codec: `ingot decode` on `mozilla` (51 MB) takes 1.9 s, but the in-memory decode only 0.2 s. The rest is `operator>>` converting the payload bit by bit and the byte-wise stream reads; see the "Copies and I/O" step.

## Encode bit buffer (2026-11-19)

After the "Encode bit buffer" step: `Engine::encode` packs codes into bytes through a 64-bit buffer instead of appending a `bitset` per symbol. Results are in `docs/benchmarks/encode-bit-buffer.json`; compare later steps against that file. Before and after ran back to back with load averages of 23 and 67, so expect noticeable noise on speeds.

- **Encode:** 210–960 MiB/s, up from 25–138: 2.5–38× faster. The gain grows with the average code length, since the old encoder appended bit by bit: largest on the nearly incompressible files (`flower.png` 38×, `lena.jpg` 29×, `sao` 17×), smallest on `ptt5` (2.5×) and small files, where counting frequencies and building the codes take a larger share. `Engine` now encodes faster than zlib's Huffman-only mode (130–230 MiB/s) on every file.
- **Decode:** unchanged within noise (0.96–1.41×). Dropping the bit-reversal lookup barely matters next to the decode table.
- **Ratio:** unchanged; the encoded bytes are identical to before (checked on `mozilla`).
- **CLI:** `ingot` on `mozilla` (51 MB) encodes in 0.3 s and decodes in 0.85 s (was 1.9 s, since `operator>>` no longer converts bit by bit). The remaining decode time is mostly the byte-wise stream I/O for the "Copies and I/O" step.

## Copies and I/O (2026-11-19)

After the "Copies and I/O" step: `operator>>` reads the rest of the stream in 64 KiB blocks into a buffer reserved from the remaining size, and `operator<<` writes each buffer with one `write`, instead of `istreambuf_iterator`/`ostream_iterator` byte by byte. The codec and the encoded bytes are unchanged. End-to-end numbers come from `tools/bench-cli.sh` (hyperfine, 10 runs, release, load average about 7); results are in `docs/benchmarks/copies-and-io.json`. "Before" is the previous commit, timed with the same script's python3 fallback.

| Input | Size | Encode before | Encode after | Decode before | Decode after |
|---|---:|---:|---:|---:|---:|
| canterbury/kennedy.xls | 1.0 MB | 0.011 s | 0.006 s | 0.019 s | 0.006 s |
| silesia/webster | 41 MB | 0.180 s | 0.096 s | 0.597 s | 0.192 s |
| silesia/mozilla | 51 MB | 0.257 s | 0.149 s | 0.800 s | 0.223 s |

- **Decode:** 3.1–3.6× faster end to end. On `mozilla` the CLI (0.22 s) is now close to the in-memory decode (about 0.2 s), so file I/O no longer dominates.
- **Encode:** 1.7–1.9× faster.
- **Memory:** peak RSS on `mozilla` is 93 MB, about input plus output (51 + 40 MB), so no stage holds an extra full copy.

## Stored fallback (2026-11-19)

After the "Stored fallback" step: the encoded file starts with a method byte, and the encoder stores the input as is when that gives the smaller file ([file_format.md](file_format.md)). `Engine` on the samples and Canterbury sets, release, load average about 20; results are in `docs/benchmarks/stored-fallback.json`.

- **Incompressible input no longer grows by more than the 9-byte header:** `flower.png` 734,560 → 734,569 B (ratio 1.0000, was 1.0001 with Huffman), `lena.jpg` 91,814 → 91,823 B (1.0001, was 1.0005). Both are stored.
- **Stored speed:** encode 1,450–2,700 MiB/s (counting frequencies and building code lengths, then one copy), decode 42,000–72,000 MiB/s (one copy).
- **Every other file:** still Huffman; the header grows by 1 byte to 137, and ratios are unchanged at three decimals.

## Block streaming (2026-11-19)

After container step 2 ([roadmap.md](roadmap.md#container-format-and-streaming)): the CLI streams 1 MiB blocks instead of holding whole files, and writes a temporary file that it renames over the output. Three builds were measured back to back on an Apple M3 Max under load average 10–28, so differences under about 10% are noise: `fcf63ab` (before CRC32), `18b8d64` (container step 1, CRC32) and this step. In-memory results are in `docs/benchmarks/block-streaming.json`, CLI results (hyperfine, 10 runs) in `docs/benchmarks/block-streaming-cli.json`.

- **Memory:** the CLI's peak RSS on `mozilla` (51 MB) drops from 89 MiB to 5 MiB, for encode and decode alike. It no longer depends on the file size.
- **CRC32 cost (step 1, not measured then):** on Huffman blocks, encode and decode get 10–15% slower (geomean on Canterbury and Silesia). Stored blocks are now limited by the CRC: decode drops from about 70,000 MiB/s (one copy) to about 1,900 MiB/s, which is the slicing-by-8 CRC's own speed; stored encode drops from 1,700–2,700 to 850–1,150 MiB/s.
- **Streaming itself:** the in-memory `Engine` numbers are unchanged within noise.
- **CLI time:** on `mozilla`, encode 172 → 181 ms (+5%, within σ) and decode 265 → 294 ms (+11%). The decode gap is the file system, not the codec: the old binary truncates the existing output in place, the new one creates a new file and deletes the old one on rename. With the output removed before each run, both take 298–304 ms. The encoded bytes are identical to step 1's.

## Hardware CRC32 (2026-11-19)

`crc32()` switched from slicing-by-8 to ARMv8 `crc32x` (then to runtime dispatch, which measured the same) ([roadmap.md](roadmap.md), known issue "CRC32 caps stored blocks"). In-memory `Engine` benchmarks, 3 repetitions (medians), Apple M3 Max under load average 9–46, so differences under about 10% are noise. Before: `docs/benchmarks/hardware-crc32-before.json`, after: `docs/benchmarks/hardware-crc32.json`.

- **Stored blocks** (`flower.png`, `lena.jpg`): decode about 2,100 → 7,600–8,000 MiB/s, now limited by one serial `crc32x` chain rather than the table lookups; encode 940–1,250 → 1,470–2,200 MiB/s.
- **Huffman blocks:** decode geomean +13% on Canterbury, +9% on Silesia; encode +20% and +19%. This wins back most of the 10–15% that the CRC cost in container step 1.

## Multithreaded blocks (2026-11-19)

`ingot -T <n>` on all of Silesia joined into one 212 MB file (`cat corpus/silesia/*`), hyperfine, 5 runs, medians; Apple M3 Max (10 performance + 4 efficiency cores) with three QEMU VMs and syncthing using about 3–4 cores, load average 18–100. Results in `docs/benchmarks/multithreaded-blocks-cli.json`. The encoded file is byte-identical for every thread count.

| Threads | Encode | Decode |
| --- | --- | --- |
| 1 | 484 ms (418 MiB/s) | 818 ms (247 MiB/s) |
| 2 | 228 ms (2.1×) | 411 ms (2.0×) |
| 4 | 212 ms (2.3×) | 282 ms (2.9×) |
| 8 | 217 ms (2.2×) | 278 ms (2.9×) |
| 14 | 214 ms (2.3×) | 300 ms (2.7×) |

- **Output, not the coders, is the limit:** at 14 threads, writing to `/dev/null` takes 57 ms to encode (3.7 GB/s, 8.5×) and 81 ms to decode (2.6 GB/s, 10×). To a new file, the extra 150–200 ms is almost all system time; `dd bs=1m` writing the same 212 MB to a new file takes 48–304 ms here (14 ms to `/dev/null`). Known issue "File output limits the CLI's speed with threads".
- **Memory:** peak RSS grows by about 4–5 MiB per thread (2 blocks in flight per thread, input and output): 14 MiB at 1 thread, 73 MiB at 14 for encode; 10 and 72 MiB for decode.

## Search (2026-11-20)

`ingot grep` on all of Silesia archived as a folder (`encode -i corpus/silesia`, 135 MB for 212 MB), hyperfine, 20 runs, medians, 14 threads; the same machine with a QEMU VM busy, load average about 13. `old_app` builds each line before matching it, `new_app` searches the decoded piece first and looks for line boundaries only around a match. Results in `docs/benchmarks/grep-cli.json`.

| Command | Before | After |
| --- | --- | --- |
| `verify` (decode and check everything) | | 103 ms |
| `grep -c zzzqqq` (no match) | 131 ms | 98 ms |
| `grep -c the` (many matching lines) | 179 ms | 158 ms |

- A search with few matches now runs at decoding speed, so searching blocks in parallel (planned as autonomous run item 5) can't make it faster; it would only help when most lines match, and it needs line counts and lines joined across block and file boundaries. Not done.
- Output matches the system grep: per-file `grep -c e` counts over Silesia, and `grep -n the webster`. macOS's BSD `grep -b` prints the match's offset, GNU grep and ours the line's.

## Search without decompressing (2026-11-20)

`compressor_benchmark --benchmark_filter=Search/`, one thread, 5 repetitions, medians; same loaded machine. `DecodeAndFind` decodes each block with `Engine::decode_block` and counts the pattern with `std::string_view::find`. `BitScan` encodes the pattern with each Huffman block's canonical code (cut to 56 bits) and counts where those bits occur in the payload at any bit offset; stored blocks are searched as they are. Results in `docs/benchmarks/search-bitscan.json`.

| File, pattern | DecodeAndFind | BitScan | Matches | Candidates |
| --- | --- | --- | --- | --- |
| dickens, `the` | 47.9 ms | 9.0 ms | 116 387 | 128 449 |
| dickens, `Copperfield` | 38.1 ms | 9.0 ms | 532 | 532 |
| webster, `Gutenberg` | 160 ms | 39.8 ms | 199 | 199 |
| xml, `<leqgeq>` | 23.0 ms | 5.4 ms | 280 | 280 |

- Scanning the compressed bits finds candidates about 4× faster than decoding finds matches (about 1 GB/s of original data against 0.25 GB/s per core). From about 8 characters on there were no false candidates; for `the`, 10% start inside a code.
- BitScan is only a filter: a candidate is a match only if it starts on a code boundary, which takes decoding to know. Its use would be skipping the blocks that have no candidates. Exact `grep` output can't skip them yet: line numbers need each skipped block's newline count (in the compressed bits those are candidates too), a match across a block boundary spans two code tables, and `-c` must know whether matches in different blocks share a line. See the roadmap item "Compressed-domain prefilter".

## Interleaved ARM CRC32 (2026-11-20)

`compressor_benchmark --benchmark_filter=Crc32/`, 5 repetitions, medians, on random bytes; same loaded machine. Results in `docs/benchmarks/crc32-chains.json`.

| Size | One `crc32x` chain | Three chains | Portable (slicing-by-8) |
| --- | --- | --- | --- |
| 1 MiB | 8.9 GB/s | 23.5 GB/s | 2.1 GB/s |
| 100 003 bytes | 8.9 GB/s | 23.2 GB/s | 2.1 GB/s |

- The hardware path now runs three independent chains over consecutive 4 KiB stretches and joins them by multiplying by x^(8·4096) mod P (zlib's `multmodp`, a constant computed at compile time), about 64 cycles per 12 KiB, so no PMULL is needed. 2.6×, as `crc32x`'s 3-cycle latency predicts. The x86 PCLMUL path is unchanged.

## File output (2026-11-20)

`ingot` on Silesia, 14 threads, hyperfine, 8 runs, medians; load average 90–150 (QEMU VMs). `before_app` is the build before this change. Results in `docs/benchmarks/file-io-cli.json`.

| Command | Before | After |
| --- | --- | --- |
| `decode` of the folder archive into a new directory (12 files) | 4461 ms | 82 ms |
| `encode -i corpus/silesia` (folder) | 1573 ms | 64 ms |
| `decode` of all of Silesia as one file (212 MB) to a new file | 316 ms | 79 ms |
| `encode` of that file to a new file | 241 ms | 68 ms |

- Two libc++ problems, measured with a small program writing 200 MiB in 1 MiB pieces: `std::ofstream` writes 4 KiB per system call (332 ms; 173 ms with a 1 MiB `pubsetbuf`), and a `std::filebuf` reopened after `close()` loses its buffer and goes through `overflow` and `fwrite` almost byte by byte (about 3.5 s). `fwrite` of whole buffers takes 35 ms. The folder paths reused one `filebuf` for every file, so every file after the first was slow both ways.
- Now new files are written through `OutputFile` (`fopen` with `"wbx"`, `StdioOutputBuf` with 1 MiB, `fwrite`) and archived files are read with `fread`. Writing to a new file costs about what writing to `/dev/null` does (85 ms decode).

## Deflate (2026-11-20)

`ingot encode -f gzip` against Apple gzip 479 at `-6` (zlib's default level), each Silesia file, one run each, wall time; same loaded machine. Our LZ77 uses zlib's level-6 settings (`Lz77Options` defaults: chains of 128, nice length 128, lazy matching below 16, a quarter chain from 8). Every output decompresses with `gzip -dc` to the original. Per-file numbers in `docs/benchmarks/deflate-vs-gzip.txt`.

| All 12 files (212 MB) | Ours | gzip -6 |
| --- | --- | --- |
| Compressed | 68 141 519 bytes | 68 220 703 bytes (+0.12%) |
| Time | 7.49 s | 5.16 s |

- Same ratio as gzip within 0.3% on every file, smaller on 6 of 12; per-block choice of dynamic, fixed or stored helps `x-ray` (1.5% smaller).
- 1.45× slower overall, most on plain text (dickens 0.88 s against 0.38 s): `longest_match` is about 90% of the time. zlib's lazy-match limits, a word-at-a-time match compare and reusing the look-ahead match took webster from 2.27 to 1.52 s; zlib also skips inserting most positions inside long matches.

## Compression levels (2026-11-20)

`ingot encode -L <level>` of all of Silesia joined into one 212 MB file, then `decode` to `/dev/null`; 14 threads, 1 MiB blocks, one run each, wall time; load average about 10. Each output decodes to the input. Raw output in `docs/benchmarks/levels.txt`.

| Level | Size | Ratio | Encode | Decode |
| --- | --- | --- | --- | --- |
| 1 (Huffman only, the old behaviour) | 135 218 540 | 1.567 | 0.18–0.42 s | 0.09 s |
| 2 | 74 984 134 | 2.826 | 0.32 s | 0.12 s |
| 3 | 70 535 426 | 3.005 | 0.37 s | 0.11 s |
| 4 | 71 051 629 | 2.983 | 0.29 s | 0.12 s |
| 5 | 69 276 807 | 3.059 | 0.42 s | 0.11 s |
| **6 (default)** | 68 323 582 | 3.102 | 0.86 s | 0.11 s |
| 7 | 68 037 581 | 3.115 | 0.81 s | 0.11 s |
| 8 | 67 793 304 | 3.126 | 1.31 s | 0.11 s |
| 9 | 67 746 022 | 3.128 | 1.65 s | 0.11 s |

- LZ77 doubles the ratio at almost no cost in decoding; level 6 encodes about 6× faster than single-threaded `gzip -6` (5.2 s, 68.2 MB for the 12 files) at the same size, because blocks run on every core. The independent 1 MiB blocks cost little: level 6 here is 0.1% larger than one Deflate stream per file (item 9).
- Level 4 is worse than 3, as in zlib: its lazy search has shorter chains. The first level-1 run took 24.8 s during a load spike; the reruns are the range shown.

## Transforms (2026-11-20)

`ingot encode -L 6` and `-L 7` per Silesia file and on our own arm64 `ingot` binary (`arm64.bin`), `old_app` the build before transforms; every output decodes to the input. Raw output in `docs/benchmarks/transforms.txt`.

| File | Level 6 | Level 7 (auto) | Transform |
| --- | --- | --- | --- |
| x-ray (12-bit medical image in 16 bits) | −14.2% | −14.6% | delta |
| ooffice (x86 DLL) | −9.0% | −9.0% | x86 branches |
| mr (16-bit MRI) | −7.9% | −8.9% | delta |
| arm64.bin (Mach-O arm64) | −6.9% | −6.9% | ARM64 branches |
| the other 8 files | 0 | 0 | none kept |

- A chosen transform must beat the same block without it, so nothing gets larger; before that rule, level 6 made `mozilla` 3.5% larger (a tar whose blocks mix code and data). No transform helps `mozilla` overall.
- Cost, all of Silesia as one file, 14 threads: level 6 encodes in 1.05 s instead of 0.78 s, level 7 in 1.58 s instead of 0.85 s (a second or more encodings of the blocks where a transform looks promising). Decoding is unchanged.

## BWT coder (2026-11-20)

`ingot encode -L 7` against `-L 8` (which adds the BWT coder to every block's candidates) per Silesia file, and `bzip2 -9` (900 KB blocks, several Huffman tables per block); times are wall seconds, 14 threads, one run. Every output decodes to the input. Raw output in `docs/benchmarks/bwt.txt`.

| File | Level 7 | Level 8 | Change | bzip2 -9 |
| --- | --- | --- | --- | --- |
| dickens | 3 873 654 | 2 956 373 | −23.7% | 2 799 520 |
| mozilla | 19 066 830 | 18 479 116 | −3.1% | 17 914 392 |
| mr | 3 336 092 | 2 714 413 | −18.6% | 2 441 280 |
| nci | 3 180 113 | 1 881 509 | −40.8% | 1 812 734 |
| ooffice | 2 807 076 | 2 578 590 | −8.1% | 2 862 526 |
| osdb | 3 700 355 | 3 046 092 | −17.7% | 2 802 792 |
| reymont | 1 848 294 | 1 338 333 | −27.6% | 1 246 230 |
| samba | 5 439 109 | 4 805 293 | −11.7% | 4 549 759 |
| sao | 5 322 284 | 5 316 021 | −0.1% | 4 940 524 |
| webster | 12 202 876 | 9 077 216 | −25.6% | 8 644 714 |
| x-ray | 5 094 746 | 4 583 413 | −10.0% | 4 051 112 |
| xml | 680 542 | 457 310 | −32.8% | 441 186 |

- BWT wins on everything but `sao` (star catalogue records), and beats bzip2 on `ooffice` thanks to the x86 filter. bzip2 stays 2–11% smaller elsewhere: it switches between up to six Huffman tables every 50 symbols, where we use one per block.
- Encoding at level 8 takes 1.1–6× level 7's time (the suffix sort is O(n log n) prefix doubling, and every candidate transform is tried with both coders); decoding stays at 0.04–0.13 s per file.

## rANS (2026-11-20)

One block at a time (1 MiB), single thread, per Silesia file: the BWT coder's last stage as one Huffman code against static order-0 rANS (scratch harness over `bwt_encode`), and order-0 rANS on the bytes after `choose_transforms()` against LZ77 at level 6.

- **BWT + rANS against BWT + Huffman**: smaller on all 12 files, by 0.3% (osdb) to 1.1% (webster, dickens), and 10-20% faster to decode. The Huffman stage was dropped and coder 3 is BWT + rANS.
- **rANS alone against LZ77**: larger on 11 files (dickens +48%, text up to 5×), smaller on every block of x-ray (−3.0%: noisy 12-bit samples after a delta, where LZ77 finds nothing to match and Huffman can't spend fractions of a bit). Kept as coder 4, tried only after a transform: level 6 on x-ray is now 4 962 127 bytes instead of 5 114 232.
- The gains are small because both coders are order-0. Context modelling (item 13's LZMA-style coder, or adaptive models after the BWT as in bsc) is where the next larger gain is.

## LZR coder (2026-11-20)

`ingot encode -L 8` per Silesia file before (LZ77 and BWT, `docs/benchmarks/bwt.txt`) and after adding the LZR coder (LZ77 over the whole 1 MiB block with LZMA's range-coded model); 14 threads, one run, wall seconds. Every output decodes to the input. Raw output in `docs/benchmarks/lzr.txt` (encode times before LZR's search was capped at level 7's for level 8).

| File | Level 8 before | Level 8 with LZR | Change |
| --- | --- | --- | --- |
| mozilla | 18 479 116 | 16 006 226 | −13.4% |
| sao | 5 316 021 | 4 878 849 | −8.2% |
| samba | 4 805 293 | 4 599 962 | −4.3% |
| ooffice | 2 578 590 | 2 481 195 | −3.8% |
| the 8 others | | | −0.3% to −1.3% (rANS after the BWT) |

- Per 1 MiB block on one thread, LZR is 7.6–18% smaller than Deflate on every file and beats the BWT coder on the binaries above, the files where xz beats bzip2.
- It encodes at about 5 MB/s per core. With the search capped at level 7's, level 8 takes 1.1 s for dickens and 4.9 s for x-ray (4 candidate transforms, 4 coders each), within 0.4% of the uncapped sizes; level 9 searches longest and took 19 s for x-ray. Decoding stays at 0.03–0.11 s per file.

## Context mixing experiment (2026-11-20)

Superseded by the CM coder below; `ContextMixing/` now benchmarks that coder.

`compressor_benchmark --benchmark_filter=ContextMixing/`: per 1 MiB block, one thread, an lpaq-style coder in `compressor_benchmark/src/cm/` (hashed order-1 to order-6 bit predictors with adaptive rates, a logistic mixer selected by the partial byte, a binary arithmetic coder), checked by decoding every block. Compared with the BWT coder and LZR (level 7's search) on the same blocks.

| File | Context mixing | BWT | LZR |
| --- | --- | --- | --- |
| dickens | 3 011 927 | **2 924 661** | 3 485 145 |
| xml | 967 297 | **453 440** | 558 288 |
| samba | 6 260 135 | 4 861 905 | **4 749 269** |
| sao | 4 999 331 | 5 316 723 | **4 893 543** |
| x-ray | **4 097 154** | 4 564 666 | 5 077 855 |
| ooffice | 2 841 039 | 2 953 073 | **2 821 560** |

- It wins only on x-ray (10% below BWT), and runs at 3–5 MB/s per core in both directions with 12 MiB of tables per thread, against the agreed limit of about twice the block size for decoding. So it stays an experiment, not a pipeline coder.
- What it lacks is a match model: xml and samba repeat long strings that BWT and LZR exploit and an order-6 context can't reach. lpaq adds one, plus secondary estimation; that is the next step if it is picked up again. PPM wasn't tried separately: context mixing generalises it, and order-N PPM would land between these numbers.

## Search index (2026-11-20)

All of Silesia archived with and without `--search-index`, then `search -c` (FM-index) against `grep -c` (decode and match), hyperfine medians of 5 runs, 14 threads; load average around 10.

| | Plain archive | With search index |
| --- | --- | --- |
| Size | 66 874 374 | 164 962 837 (+147%) |
| Encode | 1.25 s | 16.70 s |

| Pattern (matches) | `search -c` | `grep -c` |
| --- | --- | --- |
| Gutenberg (199) | 194 ms (1825 ms on one thread) | 98 ms |
| e (millions) | 734 ms | 113 ms |

- Results are exact: per-file counts equal `grep -o | uniq -c` on Silesia, and offsets equal `grep -rbo` on `docs/`, duplicates included.
- It is slower than grep: every query loads the whole index, decoding each chunk's stored BWT (rANS, move-to-front) and rebuilding its rank tables, which is O(data) like decoding the archive itself, and listing each match walks up to 63 LF steps. The index also stores sampled positions (1/16 of the data) and the overlaps. An FM-index pays off when it stays loaded across many queries, or with a layout searched in place: the BWT uncompressed with rank blocks or a wavelet tree, read with `mmap`, so a query touches O(pattern length) blocks. Encoding builds each chunk's suffix array on the reading thread, hence the 13×.

## Search index in place (2026-11-20)

The search index rewritten to be searched without loading it (section version 2: each chunk's BWT in a wavelet matrix with rank directories, read through `mmap`), built on the thread pool while archiving. All of Silesia, `encode -T 6`, then `search -c` against `grep -c`, hyperfine medians of 7 runs; load average about 9.

| | Before | Now |
| --- | --- | --- |
| Archive with index | 164 962 837 | 346 360 375 (plain: 66 874 374) |
| Encode with index | 16.70 s | 5.94 s (plain: 2.76 s) |

| Pattern (matches) | `search -c` before | now | `grep -c` |
| --- | --- | --- | --- |
| Gutenberg (412) | 194 ms | 9.2 ms | 126 ms |
| Copperfield | | 10.7 ms | 124 ms |
| `<leqgeq>` | | 9.6 ms | 146 ms |
| the (395 919) | | 141 ms | 143 ms |
| e (5 566 720) | 734 ms | 171 ms | 135 ms |

- A query costs time in the pattern's length and its matches: a selective pattern is found 12–15× faster than by decoding. Locating a match walks up to 63 rows back (about 0.7 µs each), so above 100 000 matches (counted first, which costs almost nothing) `search` decodes and scans instead, as fast as `grep`. Both give the same offsets: per-file counts equal `grep -o` on Silesia.
- The index is now larger: the BWT is stored uncompressed (1 byte per byte, plus 1/16 for the rank directories) where version 1 stored it BWT-coded and decoded it per query. Counts per file must be scanned for `e` because the index only knows where matches are, not which file's.

## Multithreaded Deflate (2026-11-20)

`ingot encode -f gzip|zip -T <n>` to `/dev/null`, hyperfine, 3 runs after a warm-up, mean wall time. Another project's test suite kept the load average between 40 and 55 on the 14 cores, so absolute times are high and noisy; the speed-ups are the point. `pigz` isn't installed; Apple gzip 479 is the reference.

| Input | `-T 1` | `-T 6` | Speed-up |
| --- | --- | --- | --- |
| Silesia joined (212 MB), gzip | 14.8 s | 3.2 s | 4.7× |
| Silesia folder (12 files), zip | 13.8 s | 3.6 s | 4.1× |
| 5000 files of 6000 bytes, zip | 1.01 s | 0.22 s | 4.7× (remeasured, see below) |

- Streams are deflated in 256 KiB pieces, each primed with the 32 KiB before it and ending in an empty stored block (zlib's sync flush), so the output is the same for every thread count and 5 bytes per piece larger: 68 152 076 bytes for joined Silesia against `gzip -6`'s 68 227 961 (5.40 s, single-threaded). `gzip -dc` gives back the input.
- The first run of the small files gave 1.63 s and 0.82 s (2.0×) during a load spike. Remeasured with hyperfine, 10 runs after 2 warm-ups: 1.01 s and 0.22 s, near the 0.19 s that scanning and reading the 5000 files takes (an ingot encode at level 1 of the folder). Setting up the 5000 match finders takes 0.03 s of their 0.96 s of deflating, so the suspected cost of clearing their tables isn't one.

## Context mixing coder (2026-11-20)

Coder 6 (`cm/cm.cpp`), run 2 item 2. Tuning: a scratch harness coding each Silesia file in 1 MiB blocks with `cm_encode()` alone, against level 8 (the smallest of LZ77, BWT and LZR per block with transforms, `docs/benchmarks/lzr.txt`):

| Step | Silesia total | Against level 8 |
| --- | --- | --- |
| Orders 0–4 and 6, word, match model, one mixer, two APMs | 51 095 973 | −5.0% |
| A second mixer, selected by the previous byte | 48 920 672 | −9.0% |
| Mixer rate 2 instead of 6 | 48 216 346 | −10.4% |
| Hashed counters limited to 20 updates (was 1023) | 47 964 248 | −10.8% |
| (not kept) order 5 as well | +0.1% | |
| (not kept) 4 MiB table / 64 MiB table | −8.1% / −11.3% | |

Then `ingot encode -L 9 -T 6` per file, `decode -T 6`, every output compared with the input; load average 50–60 from another project's tests, so times are high:

| File | Level 8 | Level 9 | Change |
| --- | --- | --- | --- |
| dickens | 2 924 828 | 2 521 003 | −13.8% |
| mozilla | 16 006 226 | 15 079 037 | −5.8% |
| mr | 2 700 713 | 2 309 706 | −14.5% |
| nci | 1 872 822 | 1 711 889 | −8.6% |
| ooffice | 2 481 195 | 2 153 975 | −13.2% |
| osdb | 3 037 108 | 2 654 429 | −12.6% |
| reymont | 1 320 537 | 1 142 877 | −13.5% |
| samba | 4 599 962 | 3 688 916 | −19.8% |
| sao | 4 878 849 | 4 510 861 | −7.5% |
| webster | 8 978 870 | 7 625 564 | −15.1% |
| x-ray | 4 530 042 | 3 875 376 | −14.5% |
| xml | 453 547 | 384 029 | −15.3% |
| **Total** | 53 784 699 | **47 657 662** | −11.4% |

- Against other tools on the same files: `xz -9` 48 795 480 (level 9 is 2.3% smaller), `bzip2 -9` 54 506 769 (12.6%).
- Encoding all 12 files took 300 s and decoding 44 s (about 5 MB/s with 6 threads); CM codes about 1–2 MB/s per thread in both directions, so level 9 is for archiving, not for data decoded often.

## Median image predictor (2026-11-20)

`ingot encode -L <level> -T 6` before (`old_app`, the CM commit) and after transform 5, the JPEG-LS median predictor with the row length found by sampling; every output decodes to the input. Images from `tools/fetch-corpus.sh images`: `photo.bmp` 1999² 24-bit (Sonoma), `gradient.bmp` 1501² (Radial Sky Blue), `abstract.bmp` 1777² (iMac Blue), `icon.tiff` 1024² RGBA (Safari's icon).

| File | Level 6 | Level 8 | Level 9 |
| --- | --- | --- | --- |
| abstract.bmp | 4 756 567 → 3 138 583 (−34.0%) | 3 202 843 → 3 138 293 (−2.0%) | 2 760 146 → 2 754 426 (−0.2%) |
| gradient.bmp | 2 654 300 → 2 425 921 (−8.6%) | 2 245 058 → 2 239 939 (−0.2%) | 1 959 489 → 1 940 814 (−1.0%) |
| icon.tiff | 366 421 → 340 104 (−7.2%) | 217 619 → 190 762 (−12.3%) | 216 610 → 182 046 (−16.0%) |
| photo.bmp | 4 024 560 → 4 018 897 (−0.1%) | unchanged | unchanged |
| Silesia mr | 3 380 155 → 3 097 741 (−8.4%) | | 2 309 706 → 2 159 861 (−6.5%) |
| Silesia x-ray | 4 962 127 → 4 970 132 (+0.2%) | | unchanged |

- Per 1 MiB block with each coder, the median predictor beat Paeth on every image by 1–8%, so Paeth was dropped. On the smoothed photo, coding the raw bytes with BWT or CM beats any predictor; there the predictor only helps LZ77.
- Level 6 on the abstract picture first gained only 0.7%: its bytes look like dense x86 and ARM64 calls, the branch filters came first and level 6 keeps one candidate. A predictor that passes the entropy test now comes before them. `ooffice`, `mozilla`, `osdb` and our arm64 binary came out byte-identical at levels 6 and 9.
- Cost: the row search and a second coding make level 6 on the photo 0.82 s instead of 0.54 s; Silesia joined is unchanged within noise (2.8 s against 3.3 s, load average 35–60).

## Comparison with other compressors (2026-11-20)

`tools/compare.py` (autonomous run 3, item 2), first run at 4593a2a: each corpus as one tar, wall time of the command line tools with file output, ingot, zstd and xz on 6 threads, the rest on one; Apple M3 Max, 14 cores. Full numbers in `docs/benchmarks/history/2026-11-20-4593a2adf.json`.

| Silesia (212 MB) | size | ratio | encode MB/s | decode MB/s |
| --- | ---: | ---: | ---: | ---: |
| ingot -L 1 | 135,231,487 | 1.57 | 1823 | 1160 |
| ingot -L 6 | 66,632,520 | 3.18 | 84 | 1025 |
| ingot -L 9 | 47,658,754 | 4.45 | 4.4 | 11.6 |
| gzip -6 | 68,228,577 | 3.11 | 47 | 817 |
| zstd -3 | 66,217,820 | 3.20 | 1649 | 956 |
| zstd -19 | 52,848,640 | 4.01 | 15 | 1141 |
| xz -6 | 49,498,572 | 4.28 | 16 | 501 |
| xz -9 | 48,772,876 | 4.35 | 4.1 | 127 |
| bzip2 -9 | 54,591,623 | 3.88 | 20 | 59 |
| brotli -q 9 | 56,413,609 | 3.76 | 10 | 629 |

| Ratio | ingot -L 6 | ingot -L 9 | gzip -6 | zstd -19 | xz -9 | bzip2 -9 | brotli -q 9 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Canterbury (2.8 MB) | 3.84 | 6.65 | 3.86 | 5.52 | 5.82 | 4.97 | 4.82 |
| Images (32 MB) | 3.54 | 4.43 | 2.24 | 2.79 | 3.23 | 3.74 | 2.59 |

- Level 6 compresses like `gzip -6` and `zstd -3` (it is Deflate per block, plus other choices per block), decodes faster than gzip, but encodes 20× slower than zstd -3.
- Level 9 has the best ratio on all three sets (2.3% below `xz -9` on Silesia, 12% on Canterbury, 27% on the images, where the median predictor and CM work), but decodes at 6–12 MB/s against xz's 100+: the price of context mixing.
- Canterbury is 3 blocks, so only 3 threads work: level 9 encodes it at 2.1 MB/s.
- The harness found level 9 spending 17 s per MiB of a photo in LZR's match search (4096-entry chains over a 1 MiB window); LZR now searches like level 7 at every level (4593a2a): a 4 MiB photo encodes 3× faster with the same output, Silesia per file at level 9 in 61 instead of 127 s with the same size.

## Entropy coders: BWT + CM and tANS (2026-11-20)

Autonomous run 3, item 3. Two coders, both measured per 1 MiB block (`compressor_benchmark`'s `BwtCm/*` and `Order0/*`) and end to end; the machine was busy with other work throughout, so times are user CPU seconds or relative.

**BWT + CM (coder 7)**, the bcm approach: the BWT's output coded bit by bit with order 1 at two rates, hashed order 2, a run model, one mixer and one APM. Against the BWT coder (BWT, move-to-front, zero runs, rANS) it is 8–18% smaller on every Silesia file (dickens −9.8%, ooffice −9.3%, osdb −16.1%, samba −9.9%, sao −11.2%, x-ray −18.3%, xml −8.1%), and codes and decodes at about 3.5 MiB/s per thread. Tuning, per 1 MiB block on seven Silesia files:

| Variant | Size | Decode (dickens) |
| --- | ---: | ---: |
| orders 0, 1 (two rates), 2, run model, two APMs | 21,165,614 | 3.50 MiB/s |
| without the APMs | 21,334,658 | 4.57 MiB/s |
| without the APMs and order 0 | 21,337,038 | 4.82 MiB/s |
| fixed-rate 16-bit counters instead of adaptive ones | 21,378,787 | 4.74 MiB/s |
| one APM by order 1 | 21,187,783 | 3.62 MiB/s |
| one APM by run bucket and partial byte (kept) | 21,114,828 | 3.53 MiB/s |

Order 2 in 2^20 instead of 2^22 counters costs 0.07%; a branch-free `floor_shift` changed nothing (the compiler had already removed the branch). The time goes to the per-bit dependency chain (counter, stretch, mix, squash, coder, then the next bit's context), not to memory.

Measured at level 8 first. Whole corpora as one tar, 6 threads, against level 8 before:

| Level 8 | Silesia | Canterbury | Images |
| --- | ---: | ---: | ---: |
| before | 54,084,293 | 561,904 | 8,378,016 |
| with BWT + CM | 50,329,601 (−6.9%) | 470,053 (−16.3%) | 7,332,638 (−12.5%) |

On Silesia, encoding took 309 instead of 202 CPU seconds and decoding 50.5 instead of 4.6: level 8 would decode at about 4 MB/s per thread instead of 45. So coder 7 is tried only at level 9, which already decodes slowly (decided 2026-11-20), and level 8 stays as it was. At level 9 it wins only some blocks (noisy samples after a delta); alice29 and kennedy.xls come out byte-identical.

**tANS (coder 8)**, order 0, 2^11 states, two interleaved states, against order-0 rANS (2^15) per 1 MiB block:

| File | rANS size | tANS size | rANS decode | tANS decode |
| --- | ---: | ---: | ---: | ---: |
| dickens | 5,766,440 | 5,786,270 (+0.34%) | 162 MiB/s | 389 MiB/s |
| x-ray + delta | 4,961,920 | 4,989,110 (+0.55%) | 160 MiB/s | 258 MiB/s |
| mr + delta | 7,374,260 | 7,381,450 (+0.10%) | 170 MiB/s | 386 MiB/s |
| sao + delta | 7,141,430 | 7,142,770 (+0.02%) | 184 MiB/s | 261 MiB/s |
| ooffice | 4,996,090 | 5,000,690 (+0.09%) | 152 MiB/s | 394 MiB/s |

(Measured under heavy load from other work; the ratios held across runs.) Encoding is 10–20% faster too. tANS loses precision on very skewed bytes (2.3% at p = 0.99, `TansTests`). The engine tries both wherever it tried rANS (after a transform) and keeps rANS only when it is more than 0.5% smaller. Not done: tANS as the BWT coder's last stage (a new coder id for a few percent of decode speed of a coder that BWT + CM now mostly replaces), and LZ77 + tANS in the style of zstd.

## Dictionaries (2026-11-20)

Autonomous run 3, item 4. Many small similar files, each compressed on its own: the `vcpkg.json` and `portfile.cmake` of every vcpkg port, alternately into a training half (1401 files) and a test half (1400 files); dictionaries of 112,640 bytes (zstd's default) trained on the first half, totals over the second (`ingot encode -T 1 -i FILE -o -` per file).

| Test half | `vcpkg.json` (965,548 bytes) | `portfile.cmake` (2,027,107 bytes) |
| --- | ---: | ---: |
| gzip -9 | 445,004 | 975,194 |
| ingot -L 6 | 443,545 | 971,759 |
| ingot -L 6 -D (ours) | 232,583 (−47.6%) | 582,426 (−40.1%) |
| ingot -L 6 -D (zstd's dictionary) | 233,999 | 570,482 |
| ingot -L 9 -D (ours) | 214,478 | 553,260 |
| zstd -19 | 421,700 | 959,105 |
| zstd -19 -D (`zstd --train`) | 172,630 | 448,945 |

- The dictionary halves ingot's output on these files, and our trainer's dictionary is as good as zstd's for ingot (within 0.6% and 2.1%).
- zstd with its dictionary is still 20–25% smaller. Part of it is per-file overhead: an ingot file has at least 36 bytes of container (frame header, block header, pipeline descriptor, dictionary id, end block) against zstd's ~12, which is 34 KB of the 60 KB gap on `vcpkg.json` (average 690 bytes per file). The rest is that a zstd dictionary also holds entropy tables and repeat offsets, while every Deflate block here pays for its own Huffman code.
- Not done: entropy tables in the dictionary, and priming the CM coder with it (level 9's CM runs at about 1–2 MB/s, so reading a 110 KB dictionary would cost ~0.1 s per block).

### CM primed with the dictionary (autonomous run 4, item 1)

Level 9 now runs the CM coder's model over the dictionary before coding (coder 11), on the same test halves:

| Test half | ingot -L 9 -D before | ingot -L 9 -D now | zstd -19 -D |
| --- | ---: | ---: | ---: |
| `vcpkg.json` | 214,478 | 162,218 (−24.4%) | 172,630 |
| `portfile.cmake` | 553,260 | 389,448 (−29.6%) | 448,945 |

Level 9 with a dictionary is now 6% and 13% smaller than zstd's best with its own dictionary. Priming costs about 58 ms per file on both sides (110 KB at the CM coder's speed): 1400 files took 81 s to encode one by one.

### Codes in the dictionary and the single-block frame (autonomous run 4, item 1)

`ingot train` now writes Deflate codes into the dictionary (coder 12 codes with them and sends no block header), and `encode -D` writes a file that fits one block as a single-block frame (about 15 bytes less per file). Same test halves, dictionaries retrained (`ingot train`, 112,640 bytes of content):

| Test half | run 3 | + CM primed | + codes, single-block frame | zstd -19 -D |
| --- | ---: | ---: | ---: | ---: |
| `vcpkg.json`, level 6 | 232,583 | 232,583 | 191,406 (−17.7%) | 172,630 |
| `vcpkg.json`, level 9 | 214,478 | 162,218 | 141,213 (−34.2%) | 172,630 |
| `portfile.cmake`, level 6 | 582,426 | 582,426 | 520,161 (−10.7%) | 448,945 |
| `portfile.cmake`, level 9 | 553,260 | 389,448 | 368,429 (−33.4%) | 448,945 |

With these, the codes alone took `vcpkg.json` at level 6 from 232,583 to 212,410 and the frame the rest. Level 9 with a dictionary is 18% below `zstd -19 -D` on both; level 6, which decodes about as fast as zstd, is 11% and 16% above it.

## LZT coder (2026-11-20)

Autonomous run 4, item 3: LZ77 over the whole block with sequences coded by tANS, zstd's way (coder 13). Per 1 MiB block of Silesia (`compressor_benchmark --benchmark_filter=Lzt/`, level 6's match search): 5.1% smaller than LZ77 + Huffman with the same search, 9.0% larger than LZR, and decoding at 130–1100 MiB/s on one thread (LZR about 50 MB/s).

Where it competes, Silesia as one tar, 6 threads, best of 2–3 runs under some background load:

| | size | encode | decode, 1 thread |
| --- | ---: | ---: | ---: |
| level 6 without LZT | 66,639,224 | 2.6 s | 1.03 s |
| level 6 with LZT (whole block) | 63,496,943 (−4.7%) | 10.2 s | 0.53 s |
| level 6 with LZT (32 KiB window) | 66,256,494 (−0.6%) | 4.9 s | 0.67 s |
| level 7 before | 66,220,735 | 4.8 s | 1.01 s |
| level 7 with LZT (tried 4×) | 62,737,988 (−5.3%) | 25.2 s | 0.54 s |
| level 7 with LZT (tried 2×, kept) | 62,737,997 (−5.3%) | 16.8 s | 0.52 s |
| level 7 with LZT, level 6's search | 63,345,687 (−4.3%) | 12.2 s | 0.55 s |
| level 8 with LZT | 54,084,255 (−38 bytes) | 47.8 s (was 29.2) | 3.63 s |

So LZT competes at level 7 only: level 6 stays as fast to encode as before, level 8 and up have BWT, LZR and CM, which beat it. Level 7 is now 5.3% smaller than before and decodes twice as fast, for 3.5× the encode time. It still loses to `zstd -19` on Silesia (52.8 MB at 15 MB/s on 6 threads, decoding at 1.1 GB/s): our match search stops at 258 bytes and our literals are order-0.

## PPM experiment (2026-11-20, removed)

Autonomous run 4, item 3: PPMC with contexts of 5 bytes down to none (escape counted as the context's distinct bytes, full and update exclusion, a uniform order −1), hashed into 2^18 contexts per order, coded with a carry-less range coder; the code is in commit 66401b8 and removed after it. Per 1 MiB block (`Ppm/` benchmarks, under background load, so the speeds are rough):

| File | PPM | vs BWT | vs BWT + CM | vs CM | PPM decoding |
| --- | ---: | ---: | ---: | ---: | ---: |
| dickens | 2,705,990 | −7.5% | +2.5% | +7.3% | 8.7 MiB/s |
| ooffice | 2,829,610 | −4.2% | +5.5% | +16.2% | 3.5 MiB/s |
| reymont | 1,304,000 | −1.8% | +12.5% | +14.1% | 12.3 MiB/s |
| samba | 4,914,720 | +1.1% | +12.0% | +33.2% | 7.8 MiB/s |
| x-ray | 4,694,170 | +2.8% | +25.8% | +20.8% | 1.9 MiB/s |
| xml | 591,069 | +30.4% | +41.7% | +53.9% | 20.4 MiB/s |
| total | 17,039,559 | −0.3% | +13.5% | +21.2% | |

It ties the BWT coder overall but decodes 5–30× slower than it (the same trade that kept BWT + CM out of level 8), and at level 9 BWT + CM and CM beat it everywhere at about its decoding speed. Order 5 also misses long repeats (xml). A PPMd-style coder (information inheritance, SEE, unbounded orders) would do better, but would still have to beat CM, so PPM was not kept.

## Level 6 encoding speed (2026-11-20)

Autonomous run 5, item 1. Measured as CPU time (user + system, best of 3) of `ingot encode -L 6 -T 1` on Silesia as one tar, since the machine is never idle. The profile put 77–82% of level 6 in `Lz77MatchFinder::longest_match`, 14% in coding blocks a second time with a transform, and a few percent each in block writing, rANS and tANS.

| Change | CPU time | Size | Output |
| --- | ---: | ---: | --- |
| before | 15.11 s | 66,639,224 | |
| chain table indexed with a mask (a power of two at least the window) instead of `%` by the window | 9.35 s | 66,639,224 | identical |
| multiplicative hash, 16 bits instead of zlib's 15-bit shift-and-xor | 8.48 s | 66,618,753 | −0.03% |
| hash chains on 4 bytes instead of 3 (`block_options()`) | 5.39 s | 66,718,740 | +0.12% |

Level 6 is 2.8× faster per core (14 → 39 MB per CPU second); Canterbury +0.14%, the images −0.14%. Hashing 4 bytes also helped level 7 (Silesia −1.0%, Canterbury −0.7%) and left level 8 even (+0.02%, −1.1%). The gzip and zip output keep 3-byte chains: with 4, raw images (3-byte pixels) came out 4.0% larger and Silesia 0.6%. They gain the first two changes, 10.51 → 6.49 CPU seconds for `-f gzip -T 1` on Silesia, with the same output as with the 3-byte multiplicative hash (68,132,812 bytes, 0.1% below `gzip -6`). LZR's far-repeat test on pure noise came out 0.17% larger with 4-byte chains, so its 3-byte search is still what `lz77_options_for_level()` gives; the engine uses `block_options()` throughout.

`tools/compare.py` now reports CPU time beside wall time (MB per CPU second, over all threads), compares runs by it, and includes level 7.

## LZT with an optimal parse (2026-11-20)

Autonomous run 5, item 2: coder 14, LZT with a cost-based parse (forward over the block, every position trying a literal, three repeat offsets and every length of every match a 4-byte hash chain finds, priced by the previous pass's symbol counts), three repeat offsets, matches without Deflate's 258-byte limit, and compact tANS tables. Per 1 MiB block of Silesia, against coder 13 (`LztOptimal/` benchmarks):

| Setting | Size | vs coder 13 | vs LZR | Encoding, 1 thread |
| --- | ---: | ---: | ---: | ---: |
| 1 pass, depth 32 | 61,254,431 | −3.6% | +4.2% | 41 s |
| 2 passes, depth 32 | 60,540,553 | −4.7% | +3.0% | 78 s |
| 2 passes, depth 32, compact tables | 60,427,425 | −4.9% | +2.8% | |

On six files (dickens, mozilla, samba, xml, ooffice, nci), deeper chains: depth 128 −0.9% for 1.9× the time, a higher `nice` (512) −0.1% more; with zlib's good-length cutoff (a quarter of the depth once a 24-byte match is found), depth 128 is −0.9% for 1.7×. Depth 64 with the cutoff is the default. Synthetic text from six words needed the depth most (−11% from 32 to 256).

A fair yardstick for these numbers is zstd on the same independent 1 MiB pieces: `zstd -19` per whole file 52,895,350, with a 1 MiB window 55,128,905, on independent 1 MiB pieces 57,076,340. Most of the distance to `zstd -19` is the container's independent 1 MiB blocks, which buy parallel coding and random access.

Level 7 now tries coder 14 instead of 13 (raw and with the best transform); levels 8 and 9 don't (at 8 it gained 0.24% on Silesia for 58% more encoding time):

| Level 7 | before run 5 | now | Encoding (CPU, 6 threads) | Decoding (CPU, 1 thread) |
| --- | ---: | ---: | ---: | ---: |
| Silesia | 62,737,997 | 58,782,774 (−6.3%) | 103 s | 0.62 s (340 MB/s) |
| Canterbury | 691,039 | 644,657 (−6.7%) | 1.1 s | 0.01 s |
| images | 9,118,770 | 9,046,962 (−0.8%) | 44 s | 0.22 s |

Level 7 sits between `zstd -19` (52.9 MB) and level 6 (66.7 MB) on Silesia, decodes about 3× faster than xz, and stays far behind zstd in encoding speed.

## Archive order (2026-11-20)

Autonomous run 6, item 2. Folder archives now store directories first, then symlinks, then files grouped by extension and in path order within a group (`order_for_compression()`), instead of the walk's tree order. Sizes of `ingot encode -L 6/-L 9 -i FOLDER`:

| Folder | Level 6 before | after | Level 9 before | after |
| --- | ---: | ---: | ---: | ---: |
| 600 vcpkg ports (7.6 MB: `portfile.cmake`, `vcpkg.json`, patches) | 573,035 | 550,909 (−3.9%) | 417,491 | 410,989 (−1.6%) |
| `docs/` (1.1 MB) | 175,608 | 174,796 (−0.5%) | 118,433 | 117,696 (−0.6%) |
| Canterbury (2.7 MB) | 737,142 | 735,053 (−0.3%) | 425,224 | 425,026 (−0.05%) |
| `compressor/` sources (0.95 MB) | 167,470 | 167,326 (−0.1%) | 109,579 | 110,020 (+0.4%) |
| `fuzz/` (small seeds of many kinds) | 62,588 | 64,068 (+2.4%) | 55,359 | 55,245 (−0.2%) |

Ordering by extension and then file name instead of path lost on the sources (+2.2% at level 6): it split each directory's files apart, and level 6's matches reach only 32 KiB back. Choosing the transform per file wasn't built: blocks are fixed-size and span files, so it would need variable-size blocks, a format change; large files already get blocks of their own, and the grouping makes mixed blocks rarer.

## Faster decoding (2026-11-20)

Autonomous run 6, item 5, CPU time of `ingot decode -T 1`.

- **Huffman in four streams** (coder 15, for blocks of 64 KiB and up): Silesia at level 1 decodes in 0.38 CPU seconds instead of 1.00 for the same data as Huffman blocks (2.6×, about 560 MB per CPU second), for 3,094 bytes more on 212 MB; encoding is unchanged (1.0 CPU second). Four independent table lookups per step overlap; one stream is a chain of load, table lookup and shift per symbol (about 13 cycles). Rewriting the single-stream loop first (an unaligned 64-bit load per symbol, or per three symbols) gained nothing (0.81–1.15 against 0.79 seconds), so method 1 kept its loop.
- **CM mixer** (level 9): making the mixer's input count a template parameter, so the compiler unrolls and vectorizes its int64 dot product and update, gave the same time within noise (1.88–2.00 against 1.97 CPU seconds for 3 MB of dickens). Level 9 decoding is spent waiting on its hashed-context lines (`find_lines` about 20%), counters and APMs bit by bit, not on the mixer's arithmetic; the model's arithmetic is part of the format, so a faster mixer would mean new CM files. Not kept.

## GPU spike: Metal plumbing (2026-11-21)

Run 7, item 0: `gpu/gpu.h` (`gpu::device()`, shared buffers, kernels by name, `run()` waits), Metal behind `COMPRESSOR_METAL`, kernels embedded as a metallib. GPU benchmarks use wall time (`UseRealTime()`): the calling thread mostly waits, so its CPU time would flatter the GPU. A byte histogram (threadgroup atomics), Apple M3 Max (30-core GPU), the input copied into a shared buffer each time:

| Input | CPU, 1 thread | GPU |
| --- | ---: | ---: |
| 1 MiB | 0.30 ms (3.2 GiB/s) | 0.36 ms |
| 64 MiB | 19.7 ms (3.2 GiB/s) | 7.8 ms (8 GiB/s) |

A GPU round trip (encode, commit, wait) costs about 0.25 ms, as much as the CPU needs for 1 MiB of simple work: GPU paths have to give the GPU much more work per dispatch than one simple pass over one block, or batch blocks.

## GPU spike: suffix array (2026-11-21)

Run 7, item 1. The BWT coders' suffix array, by prefix doubling with radix passes, took 84–94% of the BWT coder's time on text: 75–95 ms per MiB (CPU time, one thread). The GPU would have had to beat a good CPU algorithm, not that one:

| Suffix array per MiB (CPU time) | dickens | mozilla | xml | nci | x-ray | sao |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| prefix doubling (before) | 78.9 ms | 94.1 ms | 75.3 ms | 88.8 ms | 31.4 ms | 27.9 ms |
| SA-IS (now) | 31.8 ms | 32.2 ms | 19.9 ms | 28.2 ms | 34.1 ms | 31.5 ms |

SA-IS is 2.5–3.8× faster on text and about 10% slower on binary data, where prefix doubling ends after a few rounds; its worst case is linear. The suffix array is unique, so every output stays byte for byte the same (the fuzz seeds regenerate identically). The BWT coder per MiB of dickens: 93 → 47 ms; BWT + CM: 257 → 208 ms.

The GPU version wasn't built. A GPU radix sort's scatter pass over 1M 64-bit keys took 0.39 ms (2.7 G keys/s at 16M keys); with histogram and scan a pass costs about 0.6 ms, prefix doubling needs about 8 passes per round and some 12 rounds on text, so about 60 ms per MiB, and batching blocks would give the whole GPU about 20 MB/s against about 500 MB/s for SA-IS on 14 cores.

## GPU spike: match lists for LZT (2026-11-21)

Run 7, item 2. Coder 14's encoder (level 7) spent about 75% in its match finder and 23% in the cost-based parse, and ran the finder once per pass on the same data. Two changes, both with byte-identical output (checked against the previous binary on Silesia, Canterbury and the images, and GPU against CPU by `LztOptimalTests.Gpu_match_lists_give_the_same_output_as_the_cpu`):

- **Match lists kept between passes** (`MatchCache`): the finder doesn't depend on prices, so the second pass reuses the first's lists and searches only where the first skipped.
- **Match lists from the GPU** (`kernels.metal`, `lzt_matches`), for blocks of 256 KiB and up: the CPU builds the hash chains (one cheap pass), one GPU thread per position walks its chain with the finder's rules and writes up to 8 matches; lengths are compared only up to `nice + 1`, and a position that reaches that cap or has more matches is left to the CPU, which searches it only if the parse asks (long runs are skipped by the parse anyway).

| Level 7, Silesia as one tar, 6 threads | CPU time | Wall time |
| --- | ---: | ---: |
| before | 105.7 s | 39.0 s |
| match lists kept between passes | 71.5 s | 29.0 s |
| plus the GPU's match lists | 39.8 s | 19.0 s |

Canterbury: 1.09 → 0.81 → 0.50 CPU seconds. Level 7 needs 2.7× less CPU time and finishes 2.05× sooner. While adding this, the CPU finder was found to read one byte past the block when a match reached its end; it now stops there (the output doesn't change).

## GPU spike: Deflate match search (2026-11-21, not kept)

Run 7, item 3. Level 6's match finder (`Lz77MatchFinder::longest_match`, about 77% of the time) was moved to the GPU with byte-identical output: the CPU built the hash chains, and one GPU thread per position walked its chain with the finder's rules at both depths the tokenizer uses (the full chain, and a quarter of it for the lazy look-ahead after a good match). The tokenizer then picked the result the CPU would have computed. This applied to whole blocks of 256 KiB and up without a dictionary.

| Level 6, Silesia as one tar | CPU time | Wall time |
| --- | ---: | ---: |
| CPU, 1 thread | 5.28 s | 5.24 s |
| GPU, 1 thread | 2.89 s | 4.2 s |
| CPU, 6 threads | 5.68 s | 1.05 s |
| GPU, 6 threads | 3.37 s | 1.03 s |

Output was identical on Silesia and Canterbury. It needs 1.8× less CPU time but is barely faster in wall time, short of the ≥3× criterion. The rest of level 6 (building the chains, tokenizing, Huffman, the transform trial) costs as much CPU time as the GPU saves, so a free match search caps the gain near 1.8×. With 6 threads the CPU already keeps up with the GPU. The code was removed. The GPU pays off where the CPU repeats expensive searches (level 7, item 2), not for level 6's greedy-lazy finder.

## GPU spike: Huffman encoding and decoding at level 1 (2026-11-21, not built)

Run 7, items 4 and 5, settled by measuring how much of level 1's time a GPU could remove. Silesia as one tar (212 MB), 6 threads, page cache warm:

| | CPU time | Wall time |
| --- | ---: | ---: |
| level 1 encode | 0.52 s | 0.10 s |
| copying the tar (`cp`) | 0.04 s | 0.12 s |
| level 1 decode (coder 15) | 0.33 s | 0.07 s |
| copying the 135 MB output (`cp`) | 0.02 s | 0.04 s |

With 6 threads, both directions already run as fast as copying the files: the time left is reading and writing. A GPU coder could only lower CPU time, which isn't the criterion ("beats 6 CPU threads"), and each 1 MiB block would cost at least one 0.25 ms round trip. Neither was built.

## GPU spike: exhaustive transform search (2026-11-21)

Run 7, item 6. A temporary mode made the level 7 engine code every delta stride 1–32, the median predictor for 1–4 bytes per pixel at every row length within a pixel of the detected one, and both branch filters, on every block (bytes after level 7, 6 threads):

| File | Default | Exhaustive |
| --- | ---: | ---: |
| icon.tiff | 240,713 | 226,047 (−6.1%) |
| mr (Silesia) | 2,946,384 | 2,923,439 (−0.8%) |
| gradient.bmp | 2,425,921 | 2,422,167 (−0.15%) |
| abstract.bmp, photo.bmp, 16-bit speech, x-ray, sao, osdb | | same within 4 bytes |

Only each file's first block changed. `choose_transforms` sampled the first 64 KiB of a block, which in the first block is the file header, so the picture's predictor wasn't found. It now samples from the middle of the block (test `TransformTests.A_header_at_the_block_start_does_not_hide_the_image`). That gets the exhaustive search's gain at no cost: icon.tiff −7.3% and mr −2.0% at level 6. Totals at level 6: Silesia 66,718,740 → 66,541,811 bytes (−0.27%), images 9,145,185 → 9,080,119 (−0.71%), Canterbury and level 1 unchanged. With middle sampling, the exhaustive search adds only 0.3% on gradient.bmp, so there is nothing to move to the GPU.

## GPU spike: content-defined chunking (2026-11-21)

Run 7, item 7, the front end for long-range matching (item 11). `chunk/chunk.h`: `chunk_ends()` is FastCDC (a gear hash of about the last 64 bytes, a stricter mask before the average size and a looser one after it, chunks from a quarter to 8 times the average), and `chunk_hash()` is a 64-bit hash in four lanes. On the CPU, one thread, 64 MiB of noise (`Chunk/*` benchmarks):

| Average chunk | Chunking | Chunking and hashing |
| --- | ---: | ---: |
| 512 B | 1.87 GiB/s | 1.38 GiB/s |
| 4 KiB | 1.91 GiB/s | 1.48 GiB/s |

That is faster than every level but 1 runs per core, so the GPU wasn't needed. How much of an input repeats in *other* 1 MiB blocks, where a block's own coder can't reach (chunks of 1 KiB, a prototype):

| Input | Size | Repeated in other blocks |
| --- | ---: | ---: |
| Silesia as one tar | 212 MB | 0.3% |
| Three versions of this repository's source (`git archive` of commits 60 and 30 back and of `HEAD`) | 8.6 MB | 48% |
| The release build's test, benchmark and `ingot` binaries | 24.7 MB | 41% |

On the last two, ingot level 9 gives 3,203,990 and 6,491,253 bytes, against 1,272,879 and 3,547,750 for `zstd -19 --long=27`, and 1,264,152 and 3,268,576 for `xz -9`. A prototype replaced each repeated chunk with a reference to the literal bytes of an earlier block, extending the match byte by byte in both directions. Compressing what was left at level 9 gave 1,244,978 bytes (512-byte chunks) and 4,110,671 bytes. This is the plan for item 11.
