# File format

An encoded file is one canonical Huffman code over the whole input: a fixed 136-byte header followed by the payload. It has no magic number, version or checksum yet; the container format (`docs/roadmap.md`, Features) adds those. Implemented by `operator<<`/`operator>>` on `EncodedData` in `compressor/src/compressor/data/data.cpp`.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Decoded size in bytes, unsigned, little-endian |
| 8 | 128 | Code lengths: 4 bits per byte value. Byte value `2i` is in the low nibble of byte `8 + i`, `2i + 1` in the high nibble. 0 means the byte value doesn't occur |
| 136 | rest | Payload: the code of every input byte in order, packed from the most significant bit of each byte down; the last byte is padded with zero bits |

## Code lengths

- No length exceeds `max_code_length` (12, in `compressor/include/compressor/huffman/huffman.h`). The encoder computes optimal lengths under that cap with package-merge (`code_lengths()`).
- A valid header has one of these:
  - all lengths 0, with a decoded size of 0;
  - exactly one byte value with length 1 (single-symbol input; its code is `0`);
  - at least two lengths whose Kraft sum Σ 2^-length is exactly 1 (a complete prefix code).

  Anything else is rejected, as is a non-zero size with no codes.

## Canonical codes

The codes aren't stored. Encoder and decoder rebuild them from the lengths (`canonical_codes()`, as in RFC 1951, section 3.2.2):

1. Count the codes of each length.
2. The first code of length `n` is `(first code of length n-1 + count of length n-1) << 1`, starting from 0.
3. Byte values get consecutive codes in order of (length, byte value).

Example: lengths `A=3 B=3 C=3 D=3 E=3 F=2 G=4 H=4` give `F=00 A=010 B=011 C=100 D=101 E=110 G=1110 H=1111`.

## Decoding

Read bits from the payload and emit a byte whenever the bits read so far form a code, until the decoded size is reached. The payload's bit count isn't stored. If the payload ends first, or no code matches within `max_code_length` bits, the input is corrupt.
