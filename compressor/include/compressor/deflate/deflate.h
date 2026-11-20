#ifndef Deflate_h
#define Deflate_h

#include <cstdint>
#include <expected>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

#include <compressor/error.h>
#include <compressor/lz77/lz77.h>

namespace compressor
{

// RFC 1951. What a raw Deflate stream holds, for the gzip and zip containers around it.
struct DeflateResult
{
    uint64_t input_bytes = 0;
    uint64_t output_bytes = 0;
    uint32_t crc = 0; // CRC32 of the input
};

class ThreadPool;

// Reads `in` to its end and writes it as one raw Deflate stream: LZ77 matches, then per block
// whichever of dynamic Huffman (our length-limited canonical codes), fixed Huffman or stored is
// smallest. The input is coded in pieces that end with a sync flush, on `pool` when given; the
// output is the same with or without one.
std::expected<DeflateResult, Error> deflate_stream(std::istream& in, std::ostream& out,
                                                   const Lz77Options& options = {},
                                                   ThreadPool* pool = nullptr);

// One raw Deflate stream of bytes already in memory, without pieces.
std::vector<uint8_t> deflate_bytes(std::span<const uint8_t> bytes, const Lz77Options& options = {});

struct GzipHeader
{
    std::string name;   // the original file name, or empty
    uint32_t mtime = 0; // seconds since 1970, or 0
};

// RFC 1952: a gzip member around deflate_stream().
std::expected<DeflateResult, Error> write_gzip(std::istream& in, std::ostream& out,
                                               const GzipHeader& header,
                                               const Lz77Options& options = {},
                                               ThreadPool* pool = nullptr);

} // namespace compressor

#endif /* Deflate_h */
