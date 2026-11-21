#ifndef Range_h
#define Range_h

#include <cstdint>
#include <expected>
#include <iosfwd>
#include <optional>
#include <span>
#include <streambuf>
#include <string_view>
#include <vector>

#include <compressor/error.h>
#include <compressor/stream/decoder.h>

namespace compressor
{

// Bytes [from, to) of some data; no `to` means to its end.
struct ByteRange
{
    uint64_t from = 0;
    std::optional<uint64_t> to;

    bool operator==(const ByteRange&) const = default;
};

// "FROM:TO", "FROM:" or ":TO", decimal byte offsets, TO not included; nothing for anything else
// or TO before FROM.
std::optional<ByteRange> parse_range(std::string_view text);

// Passes on only bytes [skip, skip + length) of what is written to it.
class SliceBuf : public std::streambuf
{
public:
    SliceBuf(std::streambuf* out, uint64_t skip, uint64_t length);

protected:
    int_type overflow(int_type c) override;
    std::streamsize xsputn(const char* data, std::streamsize count) override;

private:
    std::streambuf* out_;
    uint64_t skip_;
    uint64_t length_;
    uint64_t position_ = 0;
    bool failed_ = false;
};

// Where a plain ingot file's blocks start, from their headers alone (payloads are skipped by
// seeking): each block's offset, then the end block's. A single-block frame has no offsets.
struct BlockTable
{
    uint8_t block_size_log2 = 0;
    uint64_t total_size = 0;
    bool single_block = false;
    std::vector<uint64_t> offsets;
};

// `in` must be able to seek; unexpected_archive for a folder archive.
std::expected<BlockTable, Error> read_block_table(std::istream& in);

// Writes `range` of a plain ingot file's decoded bytes to `out`, decoding only the blocks that hold
// it (each still checked against its CRC). bad_range when it starts past the end; a `to` past the
// end stops at the end.
std::expected<void, Error> decode_range(IBlockDecoder& decoder, std::istream& in, ByteRange range,
                                        std::ostream& out);

} // namespace compressor

#endif /* Range_h */
