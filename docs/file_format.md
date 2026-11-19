# File format

An ingot file (`.ingot`) is a frame header, a sequence of independently coded blocks, and an end block. Each block is stored as is or coded with its own canonical Huffman code, and carries the CRC32 of its decoded bytes. All integers are unsigned little-endian. Implemented by `operator<<` and `read_encoded()` on `EncodedData` in `compressor/src/compressor/data/data.cpp`.

## Frame header (8 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `89 49 4E 47` (`\x89ING`). The high first byte catches transfers that strip the 8th bit, as in PNG |
| 4 | 1 | Format version: 1 |
| 5 | 1 | log2 of the block size, 12–26 (4 KiB–64 MiB). The encoder writes 20 (1 MiB) |
| 6 | 2 | Flags, must be 0 |

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

Empty input is the frame header followed directly by the end block, 17 bytes in all.

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
| `corrupt_header` | Block size or flags out of range; unknown method; a decoded size of 0 or above the block size; a short block that isn't the last; a stored block whose encoded size differs from its decoded size; a Huffman block with fewer than 128 encoded bytes, a payload longer than 15 bits per byte, or invalid code lengths; a total size that doesn't match the blocks; any byte after the end block |
| `corrupt_payload` | A Huffman payload ends before the decoded size is reached, no code matches within `max_code_length` bits, bytes follow the one that holds the last code, or a padding bit isn't zero |
| `checksum_mismatch` | A block decodes, but its CRC32 differs |

`compressor_app decode` reports the error on stderr, exits non-zero and doesn't create or change the output file. Decoding to standard output can't take back what is already written: the blocks before the failing one stay in the output.
