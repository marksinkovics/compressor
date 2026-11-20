# File format

An ingot file (`.ingot`) is a frame header, a sequence of independently coded blocks, and an end block. Each block is stored as is or coded with its own canonical Huffman code, and carries the CRC32 of its decoded bytes. All integers are unsigned little-endian. Implemented by `operator<<` and `read_encoded()` on `EncodedData` in `compressor/src/compressor/data/data.cpp`.

## Frame header (8 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 47` (`\x89ING`). The high first byte catches transfers that strip the 8th bit, as in PNG |
| 4 | 1 | Format version: 1 |
| 5 | 1 | log2 of the block size, 12–26 (4 KiB–64 MiB). The encoder writes 20 (1 MiB) |
| 6 | 2 | Flags. Bit 0: folder archive (see "Archive"); the other bits must be 0 |

## Block

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Method: 0 = stored, 1 = Huffman. 0xFF is the end block; 2–254 are reserved and rejected |
| 1 | 4 | Decoded size: 1 to the block size. Every block but the last is exactly the block size |
| 5 | 4 | Encoded size: the bytes after this 13-byte header (code lengths and payload), so a reader can skip the block |
| 9 | 4 | CRC32 of the decoded bytes (CRC-32/ISO-HDLC, as in gzip and zip) |
| 13 | 128 | Huffman only. Code lengths: 4 bits per byte value. Byte value `2i` is in the low nibble of byte `13 + i`, `2i + 1` in the high nibble. 0 means the byte value doesn't occur |
| 13 (stored), 141 (Huffman) | rest | Payload. Stored: the decoded bytes. Huffman: the code of every byte of the block in order, packed from the most significant bit of each byte down; the last byte is padded with zero bits |

## End block (9 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | 0xFF |
| 1 | 8 | Total decoded size: the sum of the blocks' decoded sizes |

Empty input is the frame header followed directly by the end block, 17 bytes in all. Without the archive flag, nothing may follow the end block.

## Archive

A folder archive is the container above with flag bit 0 set. Its decoded data is the contents of every regular file, back to back in entry order, so blocks span file boundaries and small files share blocks. The end block is followed by the index and a fixed-size trailer. Implemented in `compressor/src/compressor/archive/`.

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
- Files' offsets run back to back from 0 in entry order, and their sizes add up to the end block's total.

The encoder writes each directory before its contents, siblings in byte order of their names. Owners, hard links, devices, pipes and sockets aren't stored; the encoder skips special files with a warning.

### Extracting

`compressor_app decode` extracts into a new directory: it builds a temporary directory next to the output and renames it into place on success, and refuses an output that already exists or is standard output. It creates the directories, then decodes the data into the files in order (checking each file's CRC32), then creates the symlinks, then sets permissions and times, directories last. Archives can't be decoded from standard input yet, as the index is at the end.

## Choosing the method

Per block, the encoder writes whichever is smaller: stored is `size` bytes after the block header, Huffman `128 + payload`. A tie goes to stored, which decodes faster. Every code is at least 1 bit, so blocks up to 147 bytes always come out stored, and so does already-compressed data such as PNG or JPEG. A stored file is the input plus `17 + 13 × blocks` bytes.

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
| `unsupported_version` | The version isn't 1 |
| `truncated` | The file ends inside a header, a payload, or before the end block |
| `corrupt_header` | Block size out of range, or a flag other than the archive flag; unknown method; a decoded size of 0 or above the block size; a short block that isn't the last; a stored block whose encoded size differs from its decoded size; a Huffman block with fewer than 128 encoded bytes, a payload longer than 15 bits per byte, or invalid code lengths; a total size that doesn't match the blocks; any byte after the end block of a plain container |
| `corrupt_payload` | A Huffman payload ends before the decoded size is reached, no code matches within `max_code_length` bits, bytes follow the one that holds the last code, or a padding bit isn't zero |
| `checksum_mismatch` | A block decodes, but its CRC32 differs; or an archive file's contents don't match its CRC32 |
| `unexpected_archive` | A plain decode (standard input, or an in-memory read) meets the archive flag |
| `corrupt_index` | An archive's trailer, index CRC32 or entries break the rules under "Archive", or its data ends somewhere other than the index offset |
| `file_failed` | An archived file or directory can't be read, or an extracted one can't be created or written; the message names the path |

`compressor_app decode` reports the error on stderr, exits non-zero and doesn't create or change the output file. Decoding to standard output can't take back what is already written: the blocks before the failing one stay in the output.
