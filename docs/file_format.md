# File format

An encoded file is a method byte, the decoded size, and a payload coded with that method: stored as is, or one canonical Huffman code over the whole input. It has no magic number, version or checksum yet; the container format (`docs/roadmap.md`, Features) adds those. Implemented by `operator<<`/`operator>>` on `EncodedData` in `compressor/src/compressor/data/data.cpp`.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Method: 0 = stored, 1 = Huffman. 2–255 are reserved for later coders and rejected |
| 1 | 8 | Decoded size in bytes, unsigned, little-endian |
| 9 | 128 | Huffman only. Code lengths: 4 bits per byte value. Byte value `2i` is in the low nibble of byte `9 + i`, `2i + 1` in the high nibble. 0 means the byte value doesn't occur |
| 9 (stored), 137 (Huffman) | rest | Payload. Stored: the input bytes. Huffman: the code of every input byte in order, packed from the most significant bit of each byte down; the last byte is padded with zero bits |

## Choosing the method

The encoder writes whichever file is smaller: stored is `9 + size` bytes, Huffman `137 + payload` bytes. A tie goes to stored, which decodes faster. Every code is at least 1 bit, so inputs up to 147 bytes (empty input included) always come out stored, and so does already-compressed data such as PNG or JPEG.

## Code lengths

- No length exceeds `max_code_length` (15, DEFLATE's limit and the most a 4-bit field holds, in `compressor/include/compressor/huffman/huffman.h`). The encoder computes optimal lengths under that cap with package-merge (`code_lengths()`).
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

A stored payload is the decoded data. For Huffman, read bits from the payload and emit a byte whenever the bits read so far form a code, until the decoded size is reached; the payload's bit count isn't stored.

The input is corrupt if:

- the header is shorter than 9 bytes (stored) or 137 bytes (Huffman), the method is unknown, or the code lengths break the rules under "Code lengths";
- a stored payload's length differs from the decoded size;
- a Huffman payload ends before the decoded size is reached, or no code matches within `max_code_length` bits;
- bytes follow the one that holds the last code (a decoded size of 0 needs an empty payload), or a padding bit isn't zero.

`compressor_app decode` reports corrupt input on stderr, exits non-zero and doesn't create or change the output file.
