#ifndef Data_h
#define Data_h

#include <array>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <optional>
#include <vector>

namespace compressor
{

struct IData
{
    bool operator==(const IData&) const = default;
};

struct DecodedData : IData
{
    std::vector<uint8_t> data_;
};

std::ostream& operator<<(std::ostream& ostream, const DecodedData& data);
std::istream& operator>>(std::istream& istream, DecodedData& data);
bool operator==(const DecodedData& lhs, const DecodedData& rhs);

// docs/file_format.md
inline constexpr std::array<uint8_t, 4> magic{0x89, 'I', 'N', 'G'};
// Written; version 1 (no pipeline blocks) is still read.
inline constexpr uint8_t format_version = 2;
inline constexpr uint8_t min_format_version = 1;
inline constexpr uint8_t default_block_size_log2 = 20;
inline constexpr uint8_t min_block_size_log2 = 12;
inline constexpr uint8_t max_block_size_log2 = 26;
inline constexpr std::size_t frame_header_bytes = 8;
inline constexpr std::size_t block_header_bytes = 13;
inline constexpr std::size_t code_lengths_bytes = symbol_count / 2;
inline constexpr std::size_t end_block_bytes = 9;
// Frame header flag: a folder archive, whose index follows the end block.
inline constexpr uint16_t archive_flag = 0x0001;

// The values are stored in the file; 0xFF marks the end block, the rest are reserved.
enum class Method : uint8_t
{
    stored = 0,
    huffman = 1,
    pipeline = 2, // version 2: the payload starts with a pipeline descriptor
};

// Pipeline descriptor (docs/file_format.md, "Pipeline blocks"): a transform count, each transform's
// id and parameter, then the coder.
enum class Coder : uint8_t
{
    lz77_huffman = 2, // a raw Deflate stream (RFC 1951) of the transformed bytes
};
inline constexpr std::size_t pipeline_descriptor_bytes = 2; // with no transforms
inline constexpr std::size_t pipeline_transform_bytes = 5;  // id, then a 4-byte parameter
inline constexpr std::size_t max_pipeline_transforms = 4;

struct EncodedBlock
{
    Method method_ = Method::huffman;
    uint32_t size_ = 0;          // decoded size in bytes
    uint32_t crc_ = 0;           // CRC32 of the decoded bytes
    CodeLengths code_lengths_{}; // all zero when stored
    // Huffman: codes first bit most significant, zero-padded to a byte. Stored: the input itself.
    std::vector<uint8_t> payload_;

    bool operator==(const EncodedBlock&) const = default;
};

struct EncodedData : IData
{
    uint8_t block_size_log2_ = default_block_size_log2;
    std::vector<EncodedBlock> blocks_;

    bool operator==(const EncodedData&) const = default;
};

// The container written piece by piece; operator<< on EncodedData is the three in order.
void write_frame_header(std::ostream& ostream, uint8_t block_size_log2, uint16_t flags = 0);
void write_block(std::ostream& ostream, const EncodedBlock& block);
void write_end_block(std::ostream& ostream, uint64_t total_size);
// The bytes write_block() writes.
uint64_t written_size(const EncodedBlock& block);

// Reads a container one block at a time, checking every rule that needs no decoding.
class FrameReader
{
public:
    explicit FrameReader(std::istream& istream);

    // Returns the block size log2.
    std::expected<uint8_t, Error> read_header();
    // Instead of read_header(): reads blocks from the stream's position, as in a container with
    // this block size, for decoding blocks found through an archive's block table.
    void start_blocks(uint8_t block_size_log2);
    // Fills `block`, reusing its payload's capacity, and returns true; returns false once the end
    // block's total matches and, unless this is an archive, nothing follows it. An archive's
    // stream is left just after its end block. Only after read_header().
    std::expected<bool, Error> read_block(EncodedBlock& block);
    uint16_t flags() const;
    uint64_t total_size() const;

private:
    std::istream& istream_;
    uint8_t version_ = format_version;
    uint16_t flags_ = 0;
    uint64_t block_size_ = 0;
    uint64_t total_ = 0;
    std::optional<uint32_t> last_size_;
};

std::ostream& operator<<(std::ostream& ostream, const EncodedData& data);
// Reads one whole container and rejects anything after its end block.
std::expected<EncodedData, Error> read_encoded(std::istream& istream);
// read_encoded() that sets failbit on any error and leaves `data` empty.
std::istream& operator>>(std::istream& istream, EncodedData& data);

} // namespace compressor

#endif /* Data_h */
