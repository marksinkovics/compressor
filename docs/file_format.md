# File format

An ingot file (`.ingot`) is a frame header, a sequence of independently coded blocks, and an end block. Each block is stored as is, coded with its own canonical Huffman code, or (version 2) run through a pipeline such as LZ77 + Huffman, and carries the CRC32 of its decoded bytes. All integers are unsigned little-endian. Implemented by `operator<<` and `read_encoded()` on `EncodedData` in `compressor/src/compressor/data/data.cpp`.

## Frame header (8 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 47` (`\x89ING`). The high first byte catches transfers that strip the 8th bit, as in PNG |
| 4 | 1 | Format version: 2. Version 1 files (no pipeline blocks) are still read |
| 5 | 1 | log2 of the block size, 12–26 (4 KiB–64 MiB). The encoder writes 20 (1 MiB) |
| 6 | 2 | Flags. Bit 0: folder archive (see "Archive"). Bit 1: a search index follows the archive (see "Search index"; only with bit 0). Bit 2: a single-block frame (see below; not with bit 0, version 2 only). Bit 3: long-range references (see "Long-range references"; not with bit 2, version 2 only); the header is then followed by one byte, log2 of the window in bytes, from the block size's log2 to 32. The other bits must be 0 |

### Single-block frame

With flag bit 2 the frame holds exactly one block of 1 to `2^log2` bytes, in a shorter form and with no end block: the method byte, the decoded size as a LEB128 varint (7 bits per byte, low first, at most 5 bytes), the CRC32 (4 bytes), the 128 code-length bytes for a Huffman block, then the payload, which runs to the end of the file (a stored payload must be exactly the size; the limits for the other methods are as below). It saves 13 or more bytes per file. The encoder writes it only when asked to and the whole input fits one block; `ingot encode -D` asks, since dictionaries are for small files. Every reader takes it as a frame of that one block.

## Block

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Method: 0 = stored, 1 = Huffman, 2 = pipeline (version 2 only), 3 = references (only with frame flag bit 3). 0xFF is the end block; the rest are reserved and rejected |
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
| 1 | Coder: 2 = LZ77 + Huffman, 3 = BWT, 4 = rANS, 5 = LZR, 6 = CM, 7 = BWT + CM, 8 = tANS, 9 = LZ77 + Huffman with a dictionary, 10 = LZR with a dictionary, 11 = CM with a dictionary, 12 = LZ77 with the dictionary's codes, 13 = LZT, 14 = LZT with three repeat offsets, 15 = Huffman in four streams. Others are reserved |
| rest | The coder's output |

Transforms (`compressor/include/compressor/transform/transform.h`); positions count from the start of the block:

| Id | Name | Parameter | Encoding (decoding does the reverse) |
|---:|---|---|---|
| 1 | Delta | Stride `s`, 1–65535 | Each byte from `s` on minus the byte `s` before it, mod 256: stride 2 or 4 suits 16-bit samples, 3 RGB pixels, a row's width an image's rows |
| 3 | ARM64 branches | 0 | Every 4-byte-aligned little-endian word whose top 6 bits are `100101` (BL): its 26-bit word offset plus the word's index, mod 2^26 |
| 4 | x86 branches | 0 | Scanning from the start, every `E8` or `E9` byte with 4 bytes after it: those 4 bytes as a little-endian number plus the position after them, mod 2^32; the scan then continues after them |
| 5 | Median predictor | Row length `r` in bytes (bits 0–23) and pixel size `p` in bytes, 1–16 (bits 24–31), `r > p` | Each byte minus JPEG-LS's median edge prediction from `a` = the byte `p` before it, `b` = `r` before, `c` = `r + p` before (0 where that is before the block): `min(a, b)` if `c ≥ max(a, b)`, `max(a, b)` if `c ≤ min(a, b)`, else `a + b − c` mod 256. Encoding goes from the end of the block to its start, decoding from the start |

Other ids and parameters are rejected. The encoder picks a transform by sampling the block's first 64 KiB (`choose_transforms()`): a delta stride from 1, 2, 3, 4, 6, 8 or 16 that lowers the order-0 entropy by a tenth; after a stride of at most 16, the median predictor with that pixel size and the distance from 16 bytes to a third of the sample at which bytes repeat best (an image's row, or a diagonal neighbour of it), if its entropy is lower still; or a branch filter when BL words or `E8` bytes are dense enough to be machine code, unless a predictor already passed the test (images can look like calls). It codes the block both with and without it and keeps the smaller; from level 7 on (`auto_level`) it tries up to three candidates found with looser thresholds, and from level 8 (`bwt_level`) every one of them with the BWT and LZR coders as well. Level 9 (`cm_level`) also tries BWT + CM with each of them, and the CM coder without a transform and with the transform of the smallest pipeline so far.

BWT (coder 3, `compressor/src/compressor/bwt/bwt.cpp`), the bzip2 approach: the Burrows-Wheeler transform of the block with an end marker smaller than every byte, move-to-front, and rANS over the result.

| Size | Field |
|---:|---|
| 4 | Primary index: the row of the sorted rotations of `block + marker` that starts with the block itself (1 to size; 0 for an empty block) |
| 4 | Symbol count |
| rest | The symbols, rANS-coded over an alphabet of 257 (see "rANS") |

Symbols 0 (RUNA) and 1 (RUNB) write a run of zeros from move-to-front in bijective base 2, least significant digit first (RUNA is 1, RUNB 2: `run = Σ digit · 2^i`); symbol `k` from 2 to 256 is move-to-front index `k - 1`. The transformed bytes are the last column of the sorted rotations without the marker's row: row 0 (the marker's rotation) first, then the rest skipping the primary row. Decoding needs about five times the block size.

LZR (coder 5, `compressor/src/compressor/lzr/lzr.cpp`) is LZ77 over the whole block coded with LZMA's model and range coder (as in the LZMA SDK, `lc=3 lp=0 pb=2`), with these differences: no header and no end marker (the block's size ends it); only the last distance is kept (`rep0`), so after `is_match` there is one `is_rep` bit and no `rep1`-`rep3` or short-rep; distances reach the start of the block. The stream is the range coder's bytes including its 5 flush bytes, and decoding must use them all. The encoder takes matches from the same hash-chain finder as Deflate with a window as large as the block, and competes from level 8 (searching like level 7 at every level).

CM (coder 6, `compressor/src/compressor/cm/cm.cpp`) is context mixing in the style of lpaq. The first byte is log2 of the hashed contexts' table size in bytes, 16–26 (the encoder writes 24, 16 MiB); then the stream of a 32-bit binary arithmetic coder (12-bit probabilities of a 1, bytes most significant bit first, 4 flush bytes), and decoding must use all of it. The model, which encoder and decoder run identically and which therefore is part of the format, as `cm.cpp` defines it:

- Inputs per bit, in the logistic ("stretch") domain: order 0, order 1 (direct tables), orders 2, 3, 4 and 6 and the current word (hashed into 64-byte lines of 15 counters per nibble with a check word, two candidate lines, the less used one replaced), a match model (the last position of each 6-byte context; the predicted bit's counter is chosen by the match length and that bit) and a bias.
- Counters hold a 22-bit probability and a count, and move by `1 / (count + 2)` until the count reaches a limit (60 for orders 0 and 1, 20 for hashed contexts, 1023 for the match model).
- Two mixers, one selected by the match length bucket and the partial byte, one by the previous byte; their outputs are averaged, then refined by two APMs (by the partial byte, and by it and the previous byte) and averaged as `(p + apm0 + 2·apm1) / 4`.

Decoding needs the same memory as encoding: the table plus about 6 MiB, so about 22 MiB per thread, against the about twice the block size that the other coders need; it runs at about 1–2 MB/s per thread.

BWT + CM (coder 7, `bwt_cm_encode()` in `compressor/src/compressor/cm/cm.cpp`), in the style of bcm: the primary index (4 bytes, little-endian, as in BWT above), then the transformed bytes (the BWT coder's last column, before move-to-front) coded with CM's arithmetic coder and a smaller model made for BWT output; decoding must use the whole stream. The model, part of the format like CM's:

- Inputs per bit: order 1 (the previous byte and the partial byte) at two rates (counter limits 4 and 255), order 2 hashed into 2^20 counters (limit 30), a run model (when the bits so far agree with the previous byte, a counter chosen by its run length up to 63 and the bit it predicts, limit 255), and a bias. Counters, stretch and squash are CM's.
- One mixer, selected by a run bucket (no prediction, run under 4, under 16, longer) and the partial byte, rate 2; then an APM over the same context, averaged with the mixer's output as `(p + apm + 1) / 2`.

Decoding needs about 5 MiB of tables plus the inverse BWT's five times the block size.

LZT (coder 13, `compressor/src/compressor/lzt/lzt.cpp`), LZ77 coded the way zstd codes sequences. A sequence is a run of literals followed by a match (at least 3 bytes); literals after the last match end the block. The payload:

| Field | Contents |
|---|---|
| varint | Sequence count `n` (LEB128, as all varints here) |
| varint | Literal count |
| 4 streams | Each a varint byte length, then that many bytes of tANS (see "tANS"; nothing for no symbols): the literals; then per sequence the literal-run code, the match-length code, the offset code |
| rest | Extra bits, least significant bit first, per sequence those of its literal run, match length and offset in that order; zero bits to a byte, nothing after |

A literal run `r` or a match length minus 3 `v` is coded as itself below 16; above, with `t = floor(log2 v)`, as `16 + 2(t − 4) + (the bit below the top one)`, with the `t − 1` lower bits as extra bits. An offset code is 0 for the last sequence's offset again (an error in the first sequence); otherwise `1 + floor(log2 offset)`, with the bits below the top one as extra bits. Offsets reach anywhere before in the block. The encoder no longer writes coder 13 (run 5 replaced it with coder 14); it still decodes.

Coder 14 (`lzt_optimal_encode()`) is coder 13's layout with two differences. Its four tANS streams use the compact table: the number of used byte values, then for each in increasing order its gap after the previous one (the first counted from 0) and its count, all varints, together exactly 2^11. And it keeps three repeat offsets, most recent first, starting as 1, 4, 8: offset code `k` from 0 to 2 uses repeat `k` and moves it to the front; a code from 3 is the offset `2^(code − 3)` plus `code − 3` extra bits, pushed in front (the third repeat dropped). Match lengths have no upper limit (the length code covers 32 bits). The encoder finds its sequences with a cost-based parse over the whole block (two passes, the second priced by the first one's symbol counts; matches from 4-byte hash chains 64 deep), at level 7 only, raw and with the best pipeline's transform.

Huffman in four streams (coder 15) codes the block like a Huffman block, in four streams the decoder runs in step: the 128 code-length bytes as a Huffman block stores them, the byte sizes of streams 0, 1 and 2 (4 bytes each, little-endian; stream 3 is the rest), then the four streams. Stream `i` codes bytes `[i × q, (i + 1) × q)` of the block, `q = ceil(size / 4)`, MSB-first and padded with zero bits to a byte, as a Huffman block's payload. The encoder writes it instead of a Huffman block for blocks of 64 KiB and more (it decodes 2.6× faster; smaller blocks keep method 1, 14 bytes shorter).

rANS (coder 4) is the transformed bytes themselves, rANS-coded over an alphabet of 256. tANS (coder 8) is the same with tANS (below). The encoder only tries them after a transform, where they win on noisy samples such as Silesia's x-ray after a delta, and takes rANS only when it is more than 0.5% smaller than tANS, which decodes about twice as fast.

tANS (`compressor/src/compressor/entropy/tans.cpp`), static and order-0 over bytes, as zstd's FSE builds its tables: 256 frequencies as LEB128 varints (at most 2 bytes each), together exactly 2^11, or all 0 for an empty block; then the bit stream. The 2048 states are spread over the bytes as FSE does: byte `s` takes `freq(s)` slots, visited from slot 0 in steps of `2048/2 + 2048/8 + 3` modulo 2048, bytes in increasing order. Decoding slot `u` holding byte `s`, its `k`-th from 0 in table order: `x = freq(s) + k`, `bits = 11 − floor(log2 x)`, next state `(x << bits) − 2048` plus the next `bits` bits read. The bit stream is written least significant bit first into bytes, then a single 1 bit and zero padding, and is read back from that 1 bit towards the start. Two states alternate, state 0 decoding bytes 0, 2, 4, …: the reader takes state 0, then state 1 (11 bits each), then per byte the bits of its state's next state. After the last byte both states must be 0 and no bits may be left.

rANS (`compressor/src/compressor/entropy/rans.cpp`), static and order-0: the frequency of every symbol of the alphabet as a LEB128 varint (7 bits per byte, low first, at most 3 bytes), together exactly 2^15 unless there are no symbols; then, unless there are no symbols, the coder's 32-bit state written most significant byte first, then its renormalisation bytes. Decoding keeps a state `x` in [2^23, 2^31): `slot = x mod 2^15` picks the symbol `s` whose cumulative range holds it, `x = freq(s) · (x >> 15) + slot − start(s)`, then while `x < 2^23`, `x = (x << 8) | next byte`. After the last symbol, `x` must be exactly 2^23 and no bytes may be left.

LZ77 + Huffman is a raw Deflate stream (RFC 1951) of the block: matches reach back at most 32 KiB and only within the block, so blocks still decode independently and in parallel. It decodes to exactly the block's decoded size, with nothing after its final block. Decoding needs the block's own output and the payload, about twice the block size.

## End block (9 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | 0xFF |
| 1 | 8 | Total decoded size: the sum of the blocks' decoded sizes |

Empty input is the frame header followed directly by the end block, 17 bytes in all. Without the archive flag, nothing may follow the end block.

## Long-range references

With frame flag bit 3, a block can copy bytes from the blocks before it, which a block's own coder can't reach (`compressor/include/compressor/longrange/longrange.h`). Each block has *literals*: for a references block (method 3), the bytes of its own that aren't copied; for any other block, all of its bytes. Copies point only at the literals of earlier blocks, never at copied bytes, so decoding a block needs nothing but the literals of the blocks it copies from: random access (`extract`, `cat`, `cat --range`) decodes those blocks too and nothing further back. A copy may reach at most `2^window / 2^block size` blocks back, where `window` is the byte after the frame header, so a reader decoding a frame in order keeps that many blocks' literals; the encoder holds the same.

A references block's payload (its encoded size is at most `3 × size + 4096`):

| Field | Contents |
|---|---|
| varint | Reference count, at least 1 (LEB128, as all varints here) |
| per reference | 4 varints: the count of the block's own literal bytes before the copy; the source block, counted back from this one (1 is the block before; at most the window's block count); the offset into the source block's literals; the length, at least 1, within the source's literals |
| 1 | The literals' method: 0, 1 or 2, as for a block |
| varint | The literals' size; it equals the block's size minus the copies' lengths |
| 4 | CRC32 of the literals |
| 128 | Huffman only: the literals' code lengths, as in a block |
| rest | The literals' payload, as for a block of that method; an empty literals stream is stored with size 0 |

Decoding appends, for each reference in order, that many of the block's literals and then the copy, and then the rest of the literals; the result must be the block's size and match the block's CRC32. The encoder (`LongRangeMatcher`) splits each block into content-defined chunks of about 512 bytes (`chunk_ends()`), looks each up by hash among the earlier blocks' literal chunks, checks the bytes, extends the match both ways, and keeps copies of at least 32 bytes; blocks with none are written as before. `ingot encode --long[=BITS]` turns it on (window 2^28, 256 MiB, by default).

## Archive

A folder archive is the container above with flag bit 0 set. Its decoded data is the contents of every regular file, back to back in entry order, so blocks span file boundaries and small files share blocks. A file identical to an earlier one is stored once: its entry points at the earlier file's data. The end block is followed by the index and a fixed-size trailer. Implemented in `compressor/src/compressor/archive/`.

| Part | Size | Contents |
|---|---:|---|
| Index | varies | Block table, then the entries |
| Trailer | 16 | Index offset (8): where the index starts, right after the end block. Index CRC32 (4): over the index bytes. Magic `89 49 44 58` (`\x89IDX`) |

A reader seeks to the trailer, then reads the end block just before the index offset for the total size. Everything from the index offset to the trailer is the index.

### Block table

One 8-byte offset per block, `ceil(total size / block size)` of them: where the block's header starts, from the start of the file. The first is 8 (9 with long-range references), each is at least 14 bytes after the one before, and the last is at least 14 bytes before the end block. Data byte `x` is in block `x >> log2(block size)`.

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

The encoder writes all directories first, each before its contents and siblings in byte order of their names, then symlinks, then files grouped by extension (ignoring case) and in path order within each group, so that similar files are next to each other in the compressed stream (`order_for_compression()`); `list` still prints the tree's order. Archives from before this order read the same. It finds duplicates among files of the same size by CRC32 and then byte by byte, and fails if an original changes between that comparison and the archiving. Owners, hard links, devices, pipes and sockets aren't stored; the encoder skips special files with a warning.

### Extracting

`ingot decode` extracts into a new directory: it builds a temporary directory next to the output and renames it into place on success, and refuses an output that already exists or is standard output. It creates the directories, then decodes the data into the files in order (checking each file's CRC32; a duplicate is copied from the first file written with the same data), then creates the symlinks, then sets permissions and times, directories last. Archives can't be decoded from standard input yet, as the index is at the end.

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

### Dictionaries

Coders 9, 10 and 11 are coders 2, 5 and 6 primed with a dictionary (`compressor/include/compressor/dictionary/dictionary.h`): content of 1 byte to 1 MiB, optionally with Deflate codes (below), given to the encoder and decoder (`encode -D`, `decode -D`, and `extract`, `cat`, `verify`, `grep`). The payload starts with the dictionary's id, the CRC32 of the whole dictionary file (4 bytes, little-endian), then the coder's stream as if the dictionary came just before the block: for coder 9 a raw Deflate stream whose matches may reach into the dictionary's last 32 KiB (what zlib's `inflateSetDictionary` gives a raw stream), for coder 10 LZR whose matches may reach anywhere in the dictionary and whose literal model's previous and match bytes may come from it (positions for `pos_state` still count from the block), for coder 11 CM's stream after its model has run over every bit of the dictionary without coding it (predicting, then updating with the dictionary's bit), with the dictionary as the start of the match model's history. A reader without a dictionary fails with `dictionary_missing`, with another one with `dictionary_mismatch`. With a dictionary the encoder writes 9, 10 and 11 in place of 2, 5 and 6; nothing else in the container changes, so the frame doesn't say whether a dictionary was used, and each block says it for itself.

`ingot train` builds a dictionary from sample files, as described in `dictionary.h`: 8-byte strings scored by the number of samples containing them, the best 1 KiB segment of each epoch (one epoch per KiB of dictionary) kept when its score is at least half its length, best segments last.

Coder 12 needs a dictionary with codes: after the id, the block's Deflate tokens (matches reaching the dictionary's last 32 KiB, as in coder 9) coded with the dictionary's literal/length and distance codes, as in a Deflate block but without its 3 header bits or code lengths, ending with the end-of-block code and zero bits to a byte, and nothing after (`deflate_with_codes()`, `inflate_with_codes()`). From level 2 the encoder tries it beside coder 9 when the dictionary has codes, and keeps the smaller.

A dictionary file is either raw (its bytes are the content) or structured, as `ingot train` writes it:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 44` (`\x89IND`) |
| 4 | 1 | Version: 1 |
| 5 | 143 | The 286 literal/length code lengths, 4 bits each, low nibble first (as Huffman blocks store theirs) |
| 148 | 15 | The 30 distance code lengths, the same way |
| 163 | rest | The content, 1 byte to 1 MiB |

Both codes must be complete (Kraft sum exactly 1, lengths up to 15) and the end-of-block symbol must have a code; a file that starts like this but breaks a rule is taken as raw content. `train` gives every symbol a code: its counts are those of the samples matched against the content at level 6, plus one each.

## Choosing the method

Per block, the encoder writes whichever is smaller: stored is `size` bytes after the block header, Huffman `128 + payload`. A tie goes to stored, which decodes faster. From level 2 on (`ingot encode -L 2..9`, default 6) it also tries LZ77 + Huffman with zlib's match-search settings for that level, and takes it only when strictly smaller. Level 1 writes no other pipeline blocks than coder 15. Every code is at least 1 bit, so blocks up to 147 bytes always come out stored, and so does already-compressed data such as PNG or JPEG. A stored file is the input plus `17 + 13 × blocks` bytes.

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

`ingot decode` reports the error on stderr, exits non-zero and doesn't create or change the output file. Decoding to standard output can't take back what is already written: the blocks before the failing one stay in the output.

## Image file

`ingot image encode` writes an image file of its own, not a container (`compressor/include/compressor/image/image.h`). All numbers are little-endian.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 49` (`\x89INI`) |
| 4 | 1 | Version: 1 |
| 5 | 1 | Channels: 1 grey, 2 grey and alpha, 3 RGB, 4 RGBA |
| 6 | 1 | Bits per channel: 8 |
| 7 | 4 | Width, 1 to 65536 |
| 11 | 4 | Height, 1 to 65536 |
| 15 | 4 | Rows per strip, 1 to the height. The encoder writes `min(height, max(1, 2^19 / width))`, so strips hold about half a megapixel |
| 19 | 4 | CRC32 of the pixels, channels interleaved, rows top to bottom |
| 23 | rest | Each strip: its byte length (4), then its payload |

A reader rejects a file whose pixel count exceeds 32768 times its size: a pixel costs at least one coded decision of at least log2(4096 / 4095) bits, so no real file comes near that.

Strips are coded independently (any thread can decode any strip; pixels above a strip are not used). A strip's payload: the colour transform (1 byte: 0 none, 1 subtract green: G, R − G, B − G; 2 YCoCg-R; only 0 with fewer than 3 channels; alpha is never transformed), one predictor per channel (1 byte each: 0 left, 1 up, 2 the average of the two rounded down, 3 Paeth, 4 MED as in JPEG-LS, 5 GAP as in CALIC), then the stream of the binary arithmetic coder of the CM coder (12-bit probabilities, 4 flush bytes), which decoding must use exactly. The encoder picks the transform and predictors per strip by the residual entropy given the local activity on every other row.

The decoder runs the same model as the encoder, which `image.cpp` defines and which is therefore part of the format. Pixels are coded in raster order; neighbours outside the strip are replaced as in `neighbours()` (W by N, then by the middle of the range; N by W; NW, NE, NN by N; WW by W). Per pixel:

- **Copies:** the pixel may equal W, N, NE, NW or a match candidate (the last pixel whose causal neighbourhood hashed the same: a 12-pixel neighbourhood if it has a candidate, else W, N, NW, NE and WW), tried in that order, skipping one whose colour equals an earlier one's. Each try is one binary decision; on a hit the pixel is that colour and nothing else is coded for it.
- **Residuals:** otherwise each channel, in order, is predicted by its predictor plus a bias correction (the rounded mean of earlier errors in a context of the six neighbours' signs relative to the prediction and the activity, as in CALIC), and the residual, wrapped to the channel's range (8 bits, or 9 for a difference channel), is coded as a zero flag, a sign, the magnitude's bit length in unary and the bits below its leading one.
- Every decision's probability mixes counters in contexts of the local activity, the neighbours' residuals, the texture pattern, the previous channel's residual at this pixel, the gradient shape, a hash of the neighbours' exact values, and the match candidate's value relative to the prediction; a logistic mixer selected by the channel, decision kind, activity and match strength, then an APM.

## Lossy image file (experiment)

`ingot image encode --quality Q` writes the lossy experiment of run 8, item 3 (`encode_lossy_image()`); `ingot image decode` tells the two kinds apart by their magic. Grey or RGB, 8 bits per channel.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 4C` (`\x89INL`) |
| 4 | 1 | Version: 2 (version 1, a single stream, is no longer read) |
| 5 | 1 | Channels: 1 or 3 |
| 6 | 1 | Quality, 1 to 100 |
| 7 | 4 | Width, 1 to 65536 |
| 11 | 4 | Height, 1 to 65536 |
| 15 | 16 × tiles | The byte length of every stream: pass 0 of each tile in raster order, then pass 1, 2 and 3 |
| then | rest | The streams in the same order, each a stream of the CM coder's binary arithmetic coder, which decoding must use exactly |

The image is cut into tiles of 64 × 64 blocks (512 pixels; smaller at the right and bottom edges), each coded independently: neighbouring blocks outside the tile count as missing. Each tile is coded in 4 passes by zigzag band: the DCs (0), AC 1–5, 6–20 and 21–63, one stream per pass; the model carries over from one pass of a tile to the next, so a tile's passes decode in order. Because the streams are stored pass-major, a file cut after any pass of any tile still decodes (`--partial`): each tile's passes up to the first missing one, the other coefficients zero; a tile with only its DCs is shown as its block means interpolated. A region (`--region`) decodes only the tiles within 2 pixels of it (deblocking's reach), so its pixels are those of a whole decode.

RGB is converted to YCbCr (JPEG's full-range matrix) at full resolution, the image is padded to 8×8 blocks by repeating its last column and row, and each block of each channel goes through an orthonormal 8×8 DCT. The quantisers are JPEG's example tables (ITU T.81, Annex K) scaled by libjpeg's quality formula, with the chroma table first multiplied by `(120 + 9 × quality / 10) / 100`; the encoder rounds AC values with a dead zone of 0.1. In each tile and pass, blocks are coded in raster order, each block's channels in turn. Pass 0: the DC as the residual of the median predictor over the DCs of the blocks to the left, above and above-left. Passes 1–3: the count of nonzero values in the band (a binary tree as wide as the band's size needs), then the band's values in zigzag order until that many nonzeros are coded, each as a zero flag, a sign, the magnitude's bit length in unary and its lower bits. Every decision mixes counters in contexts of the zigzag position, the nonzeros left, the neighbouring blocks' magnitudes, their signed values at that position, for chroma the luma value there, and the block's energy in the bands before (`CoefficientModel` in `image.cpp`, part of the format). Decoding dequantises, inverts the DCT, smooths block edges where the step is below 0.6 times the first AC quantiser (`deblock()`), and converts back to RGB with rounding and clamping.

## Repair file

`ingot encode --repair PERCENT` writes `OUTPUT.repair` beside the output (`compressor/include/compressor/repair/repair.h`); it protects any file, so `-f gzip` and `-f zip` outputs get one too. The protected file is cut into `k` shards of `shard size` bytes (the last padded with zeros; shards of at least 64 KiB, at most 200 of them) and `m` parity shards (`ceil(k × PERCENT / 100)`, at least 1, `k + m ≤ 255`) are computed over GF(2^8) (polynomial `0x11D`) as `parity[p] = Σ_i C[p][i] · data[i]` byte by byte, with the Cauchy matrix `C[p][i] = 1 / ((k + p) XOR i)`. Any `k` intact shards of the `k + m` rebuild the file, so up to `m` damaged shards are recoverable.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 52` (`\x89INR`) |
| 4 | 1 | Version: 1 |
| 5 | 8 | Size of the protected file |
| 13 | 8 | Shard size |
| 21 | 1 | `k` |
| 22 | 1 | `m` |
| 23 | 4 × (k + m) | CRC32 of each shard (the data shards with their padding, then the parity shards) |
| … | 4 | CRC32 of everything before it |
| … | m × shard size | The parity shards |

`ingot repair FILE [-r REPAIR] [-o OUTPUT]` checks every shard's CRC32 and rebuilds the damaged data shards (in place through a temporary file, or into OUTPUT); `verify` reports what the repair file finds when `FILE.repair` exists. A cut file counts its missing shards as damaged. A damaged repair-file header is reported as `corrupt_header`; damaged parity shards just count against `m`.
