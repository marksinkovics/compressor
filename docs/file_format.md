# File format

An ingot file (`.ingot`) is a frame header, a sequence of independently coded blocks, and an end block. Each block is stored as is, coded with its own canonical Huffman code, or (version 2) run through a pipeline such as LZ77 + Huffman, and carries the CRC32 of its decoded bytes. All integers are unsigned little-endian. Implemented by `operator<<` and `read_encoded()` on `EncodedData` in `compressor/src/compressor/data/data.cpp`.

## Frame header (8 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 47` (`\x89ING`). The high first byte catches transfers that strip the 8th bit, as in PNG |
| 4 | 1 | Format version: 2. Version 1 files (no pipeline blocks) are still read |
| 5 | 1 | log2 of the block size, 12–26 (4 KiB–64 MiB). The encoder writes 20 (1 MiB) |
| 6 | 2 | Flags. Bit 0: folder archive (see "Archive"). Bit 1: a search index follows the archive (see "Search index"; only with bit 0). The other bits must be 0 |

## Block

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Method: 0 = stored, 1 = Huffman, 2 = pipeline (version 2 only). 0xFF is the end block; the rest are reserved and rejected |
| 1 | 4 | Decoded size: 1 to the block size. Every block but the last is exactly the block size |
| 5 | 4 | Encoded size: the bytes after this 13-byte header (code lengths and payload), so a reader can skip the block |
| 9 | 4 | CRC32 of the decoded bytes (CRC-32/ISO-HDLC, as in gzip and zip) |
| 13 | 128 | Huffman only. Code lengths: 4 bits per byte value. Byte value `2i` is in the low nibble of byte `13 + i`, `2i + 1` in the high nibble. 0 means the byte value doesn't occur |
| 13 (stored, pipeline), 141 (Huffman) | rest | Payload. Stored: the decoded bytes. Huffman: the code of every byte of the block in order, packed from the most significant bit of each byte down; the last byte is padded with zero bits. Pipeline: see "Pipeline blocks"; at most `size + size / 8 + 1024` bytes |

## Pipeline blocks

A pipeline block's payload starts with a descriptor naming what was done to the block's bytes, then holds the coder's output:

| Size | Field |
|---:|---|
| 1 | Transform count `n`, at most 4. Each transform turns bytes into bytes; decoding undoes them last to first |
| 5 × n | Each transform: id, then a 4-byte parameter (see below) |
| 1 | Coder: 2 = LZ77 + Huffman, 3 = BWT, 4 = rANS, 5 = LZR, 6 = CM. Others are reserved |
| rest | The coder's output |

Transforms (`compressor/include/compressor/transform/transform.h`); positions count from the start of the block:

| Id | Name | Parameter | Encoding (decoding does the reverse) |
|---:|---|---|---|
| 1 | Delta | Stride `s`, 1–65535 | Each byte from `s` on minus the byte `s` before it, mod 256: stride 2 or 4 suits 16-bit samples, 3 RGB pixels, a row's width an image's rows |
| 3 | ARM64 branches | 0 | Every 4-byte-aligned little-endian word whose top 6 bits are `100101` (BL): its 26-bit word offset plus the word's index, mod 2^26 |
| 4 | x86 branches | 0 | Scanning from the start, every `E8` or `E9` byte with 4 bytes after it: those 4 bytes as a little-endian number plus the position after them, mod 2^32; the scan then continues after them |

Other ids and parameters are rejected. The encoder picks a transform by sampling the block's first 64 KiB (`choose_transforms()`): a delta stride from 1, 2, 3, 4, 6, 8 or 16 that lowers the order-0 entropy by a tenth, or a branch filter when BL words or `E8` bytes are dense enough to be machine code. It codes the block both with and without it and keeps the smaller; from level 7 on (`auto_level`) it tries up to three candidates found with looser thresholds, and from level 8 (`bwt_level`) every one of them with the BWT and LZR coders as well. Level 9 (`cm_level`) also tries the CM coder, without a transform and with the transform of the smallest pipeline so far.

BWT (coder 3, `compressor/src/compressor/bwt/bwt.cpp`), the bzip2 approach: the Burrows-Wheeler transform of the block with an end marker smaller than every byte, move-to-front, and rANS over the result.

| Size | Field |
|---:|---|
| 4 | Primary index: the row of the sorted rotations of `block + marker` that starts with the block itself (1 to size; 0 for an empty block) |
| 4 | Symbol count |
| rest | The symbols, rANS-coded over an alphabet of 257 (see "rANS") |

Symbols 0 (RUNA) and 1 (RUNB) write a run of zeros from move-to-front in bijective base 2, least significant digit first (RUNA is 1, RUNB 2: `run = Σ digit · 2^i`); symbol `k` from 2 to 256 is move-to-front index `k - 1`. The transformed bytes are the last column of the sorted rotations without the marker's row: row 0 (the marker's rotation) first, then the rest skipping the primary row. Decoding needs about five times the block size.

LZR (coder 5, `compressor/src/compressor/lzr/lzr.cpp`) is LZ77 over the whole block coded with LZMA's model and range coder (as in the LZMA SDK, `lc=3 lp=0 pb=2`), with these differences: no header and no end marker (the block's size ends it); only the last distance is kept (`rep0`), so after `is_match` there is one `is_rep` bit and no `rep1`-`rep3` or short-rep; distances reach the start of the block. The stream is the range coder's bytes including its 5 flush bytes, and decoding must use them all. The encoder takes matches from the same hash-chain finder as Deflate with a window as large as the block, and competes from level 8 (level 8 searches like level 7; level 9 the longest).

CM (coder 6, `compressor/src/compressor/cm/cm.cpp`) is context mixing in the style of lpaq. The first byte is log2 of the hashed contexts' table size in bytes, 16–26 (the encoder writes 24, 16 MiB); then the stream of a 32-bit binary arithmetic coder (12-bit probabilities of a 1, bytes most significant bit first, 4 flush bytes), and decoding must use all of it. The model, which encoder and decoder run identically and which therefore is part of the format, as `cm.cpp` defines it:

- Inputs per bit, in the logistic ("stretch") domain: order 0, order 1 (direct tables), orders 2, 3, 4 and 6 and the current word (hashed into 64-byte lines of 15 counters per nibble with a check word, two candidate lines, the less used one replaced), a match model (the last position of each 6-byte context; the predicted bit's counter is chosen by the match length and that bit) and a bias.
- Counters hold a 22-bit probability and a count, and move by `1 / (count + 2)` until the count reaches a limit (60 for orders 0 and 1, 20 for hashed contexts, 1023 for the match model).
- Two mixers, one selected by the match length bucket and the partial byte, one by the previous byte; their outputs are averaged, then refined by two APMs (by the partial byte, and by it and the previous byte) and averaged as `(p + apm0 + 2·apm1) / 4`.

Decoding needs the same memory as encoding: the table plus about 6 MiB, so about 22 MiB per thread, against the about twice the block size that the other coders need; it runs at about 1–2 MB/s per thread.

rANS (coder 4) is the transformed bytes themselves, rANS-coded over an alphabet of 256. The encoder only tries it after a transform; on its own it wins only on noisy samples such as Silesia's x-ray after a delta.

rANS (`compressor/src/compressor/entropy/rans.cpp`), static and order-0: the frequency of every symbol of the alphabet as a LEB128 varint (7 bits per byte, low first, at most 3 bytes), together exactly 2^15 unless there are no symbols; then, unless there are no symbols, the coder's 32-bit state written most significant byte first, then its renormalisation bytes. Decoding keeps a state `x` in [2^23, 2^31): `slot = x mod 2^15` picks the symbol `s` whose cumulative range holds it, `x = freq(s) · (x >> 15) + slot − start(s)`, then while `x < 2^23`, `x = (x << 8) | next byte`. After the last symbol, `x` must be exactly 2^23 and no bytes may be left.

LZ77 + Huffman is a raw Deflate stream (RFC 1951) of the block: matches reach back at most 32 KiB and only within the block, so blocks still decode independently and in parallel. It decodes to exactly the block's decoded size, with nothing after its final block. Decoding needs the block's own output and the payload, about twice the block size.

## End block (9 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | 0xFF |
| 1 | 8 | Total decoded size: the sum of the blocks' decoded sizes |

Empty input is the frame header followed directly by the end block, 17 bytes in all. Without the archive flag, nothing may follow the end block.

## Archive

A folder archive is the container above with flag bit 0 set. Its decoded data is the contents of every regular file, back to back in entry order, so blocks span file boundaries and small files share blocks. A file identical to an earlier one is stored once: its entry points at the earlier file's data. The end block is followed by the index and a fixed-size trailer. Implemented in `compressor/src/compressor/archive/`.

| Part | Size | Contents |
|---|---:|---|
| Index | varies | Block table, then the entries |
| Trailer | 16 | Index offset (8): where the index starts, right after the end block. Index CRC32 (4): over the index bytes. Magic `89 49 44 58` (`\x89IDX`) |

A reader seeks to the trailer, then reads the end block just before the index offset for the total size. Everything from the index offset to the trailer is the index.

### Block table

One 8-byte offset per block, `ceil(total size / block size)` of them: where the block's header starts, from the start of the file. The first is 8, each is at least 14 bytes after the one before, and the last is at least 14 bytes before the end block. Data byte `x` is in block `x >> log2(block size)`.

### Entries

A 4-byte entry count, then each entry:

| Size | Field |
|---:|---|
| 1 | Type: 0 = file, 1 = directory, 2 = symlink; others are rejected |
| 2 | Permission bits, at most `07777` (ignored for symlinks when extracting) |
| 8 | Modification time: signed nanoseconds since 1970-01-01 UTC |
| 2 + n | Path: length, then the bytes |
| 8 + 8 + 4 | Files only: offset in the decoded data, size, CRC32 of the contents |
| 2 + n | Symlinks only: target, length then bytes; stored as is, never followed |

Rules, checked before anything is extracted (`corrupt_index` otherwise):

- A path is relative and `/`-separated, with no empty, `.` or `..` component and no NUL byte. Paths are unique.
- An entry's parent directory is an earlier directory entry, or the archive root. So no entry sits under a file or a symlink, and extraction never writes through a symlink.
- Files' offsets run back to back from 0 in entry order, and their sizes add up to the end block's total, except for duplicates: a non-empty file whose offset, size and CRC32 are exactly those of an earlier file that is not itself a duplicate. Duplicates add nothing to the total.

The encoder writes each directory before its contents, siblings in byte order of their names. It finds duplicates among files of the same size by CRC32 and then byte by byte, and fails if an original changes between that comparison and the archiving. Owners, hard links, devices, pipes and sockets aren't stored; the encoder skips special files with a warning.

### Extracting

`compressor_app decode` extracts into a new directory: it builds a temporary directory next to the output and renames it into place on success, and refuses an output that already exists or is standard output. It creates the directories, then decodes the data into the files in order (checking each file's CRC32; a duplicate is copied from the first file written with the same data), then creates the symlinks, then sets permissions and times, directories last. Archives can't be decoded from standard input yet, as the index is at the end.

`list`, `extract`, `cat` and `verify` read the index first. `extract` and `cat` decode only the blocks the block table gives for the selected files' byte range, checking that each block ends where the next one starts.

## Search index

With flag bit 1, the archive's trailer is followed by an FM-index section and its own trailer, so a reader finds the archive trailer just before the section (`compressor/src/compressor/fmindex/fmindex.cpp`, written with `encode --search-index`, searched in place through `mmap` by `search`). All integers are little-endian.

| Part | Size | Contents |
|---|---:|---|
| Section | varies | Version (1) = 2; chunk size (4) = 1 MiB; overlap (4) = 4096; sample rate (4) = 64; chunk count (4); a table of chunk offset (8, from the section start) and size (8) per chunk; the chunks |
| Trailer | 16 | Section offset (8). Section CRC32 (4). Magic `89 46 4D 49` (`\x89FMI`) |

The archive's decoded data is cut into chunks: while at least chunk size + overlap bytes are left, a chunk of exactly that many, the next starting chunk size later; the rest (at least the overlap) is the last chunk. So `full = total >= 1 MiB + 4096 ? (total − 4096) / 1 MiB : 0` and there are `full + 1` chunks. A match of up to 4096 bytes is in the chunk where it starts before the overlap, or in the last.

Each chunk of `n` bytes, over its sorted rotations with an end marker (`n + 1` rows, the marker's first):

| Size | Field |
|---:|---|
| 4 | `n` |
| 4 | Primary: the row whose last column is the marker (1 to `n`) |
| 256 × 4 | The first row starting with each byte value (1 + the count of smaller bytes) |
| 8 × 4 | Zeros in each wavelet-matrix level |
| 8 × (W × 8 + (W / 8 + 1) × 4) | The levels, `W = ceil(n / 64)` words each: level `l` holds bit `7 − l` of the last column (the marker's row left out), in the order the levels above sorted it (zeros first, each side in its previous order); then the ones before every 8th word |
| S × 8 | `S = ceil((n + 1) / 64)` words marking the rows whose suffix starts at a multiple of 64 (bit `r % 64` of word `r / 64`) |
| (S + 1) × 4 | The marked rows before each word |
| 4 + 4 × count | The count of marked rows, then their text positions in row order |

A query runs backward search with ranks from the wavelet matrix (eight bit ranks each) and walks each found row back to a marked one, touching only the pages it needs; `search` checks every value it reads instead of the CRC, which `verify` checks. Decoding the archive ignores the section.

## Choosing the method

Per block, the encoder writes whichever is smaller: stored is `size` bytes after the block header, Huffman `128 + payload`. A tie goes to stored, which decodes faster. From level 2 on (`compressor_app encode -L 2..9`, default 6) it also tries LZ77 + Huffman with zlib's match-search settings for that level, and takes it only when strictly smaller. Level 1 never writes pipeline blocks. Every code is at least 1 bit, so blocks up to 147 bytes always come out stored, and so does already-compressed data such as PNG or JPEG. A stored file is the input plus `17 + 13 × blocks` bytes.

## Code lengths

- No length exceeds `max_code_length` (15, DEFLATE's limit and the most a 4-bit field holds, in `compressor/include/compressor/huffman/huffman.h`). The encoder computes optimal lengths under that cap with package-merge (`code_lengths()`).
- A valid Huffman block has either exactly one byte value with length 1 (single-symbol block; its code is `0`), or at least two lengths whose Kraft sum Σ 2^-length is exactly 1 (a complete prefix code). Anything else, including no codes at all, is rejected.

## Canonical codes

The codes aren't stored. Encoder and decoder rebuild them from the lengths (`canonical_codes()`, as in RFC 1951, section 3.2.2):

1. Count the codes of each length.
2. The first code of length `n` is `(first code of length n-1 + count of length n-1) << 1`, starting from 0.
3. Byte values get consecutive codes in order of (length, byte value).

Example: lengths `A=3 B=3 C=3 D=3 E=3 F=2 G=4 H=4` give `F=00 A=010 B=011 C=100 D=101 E=110 G=1110 H=1111`.

## Decoding

A stored payload is the decoded block. For Huffman, read bits from the payload and emit a byte whenever the bits read so far form a code, until the block's decoded size is reached; the payload's bit count isn't stored. Then the CRC32 of the decoded block must match.

Errors (`compressor::Error`):

| Error | When |
|---|---|
| `not_ingot` | Shorter than 4 bytes, or the magic differs |
| `unsupported_version` | The version isn't 1 or 2 |
| `truncated` | The file ends inside a header, a payload, or before the end block |
| `corrupt_header` | Block size out of range, or a flag other than the archive flag; unknown method; a decoded size of 0 or above the block size; a short block that isn't the last; a stored block whose encoded size differs from its decoded size; a Huffman block with fewer than 128 encoded bytes, a payload longer than 15 bits per byte, or invalid code lengths; a total size that doesn't match the blocks; any byte after the end block of a plain container |
| `corrupt_payload` | A Huffman payload ends before the decoded size is reached, no code matches within `max_code_length` bits, bytes follow the one that holds the last code, or a padding bit isn't zero. A pipeline block with an unknown transform or coder, or a Deflate stream that is malformed, reaches back before the block, or decodes to more or fewer bytes than the block's size |
| `checksum_mismatch` | A block decodes, but its CRC32 differs; or an archive file's contents don't match its CRC32 |
| `unexpected_archive` | A plain decode (standard input, or an in-memory read) meets the archive flag |
| `corrupt_index` | An archive's trailer, index CRC32 or entries break the rules under "Archive", or its data ends somewhere other than the index offset |
| `file_failed` | An archived file or directory can't be read, or an extracted one can't be created or written; the message names the path |

`compressor_app decode` reports the error on stderr, exits non-zero and doesn't create or change the output file. Decoding to standard output can't take back what is already written: the blocks before the failing one stay in the output.
