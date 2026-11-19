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

The CLI is now limited by I/O, not by the codec: `compressor_app decode` on `mozilla` (51 MB) takes 1.9 s, but the in-memory decode only 0.2 s. The rest is `operator>>` converting the payload bit by bit and the byte-wise stream reads; see the "Copies and I/O" step.

## Encode bit buffer (2026-11-19)

After the "Encode bit buffer" step: `Engine::encode` packs codes into bytes through a 64-bit buffer instead of appending a `bitset` per symbol. Results are in `docs/benchmarks/encode-bit-buffer.json`; compare later steps against that file. Before and after ran back to back with load averages of 23 and 67, so expect noticeable noise on speeds.

- **Encode:** 210–960 MiB/s, up from 25–138: 2.5–38× faster. The gain grows with the average code length, since the old encoder appended bit by bit: largest on the nearly incompressible files (`flower.png` 38×, `lena.jpg` 29×, `sao` 17×), smallest on `ptt5` (2.5×) and small files, where counting frequencies and building the codes take a larger share. `Engine` now encodes faster than zlib's Huffman-only mode (130–230 MiB/s) on every file.
- **Decode:** unchanged within noise (0.96–1.41×). Dropping the bit-reversal lookup barely matters next to the decode table.
- **Ratio:** unchanged; the encoded bytes are identical to before (checked on `mozilla`).
- **CLI:** `compressor_app` on `mozilla` (51 MB) encodes in 0.3 s and decodes in 0.85 s (was 1.9 s, since `operator>>` no longer converts bit by bit). The remaining decode time is mostly the byte-wise stream I/O for the "Copies and I/O" step.

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
