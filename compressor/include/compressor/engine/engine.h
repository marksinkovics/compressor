#ifndef Engine_h
#define Engine_h

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>

namespace compressor
{

class IEngine
{
public:
    virtual ~IEngine() = default;

    virtual void print_graph() const = 0;
    virtual void print_dict() const = 0;

    virtual EncodedData encode(const DecodedData& data) = 0;
    virtual std::expected<DecodedData, Error> decode(const EncodedData& data) = 0;
};

class Engine : public IEngine
{
public:
    explicit Engine(uint8_t block_size_log2 = default_block_size_log2);

    // Graphviz dot of the code tree of the last block encoded or decoded, rebuilt from the codes.
    void print_graph() const override;
    void print_dict() const override;

    // Splits the input into blocks of the engine's block size.
    EncodedData encode(const DecodedData& data) override;
    std::expected<DecodedData, Error> decode(const EncodedData& data) override;

    // Uses whichever of stored and Huffman gives the smaller block.
    EncodedBlock encode_block(std::span<const uint8_t> bytes);
    // Appends the decoded bytes to `out`. Fails unless a stored payload is block.size_ bytes, or a
    // Huffman payload holds exactly block.size_ codes followed by zero padding, and the bytes
    // match block.crc_.
    std::expected<void, Error> decode_block(const EncodedBlock& block, std::vector<uint8_t>& out);

private:
    std::expected<void, Error> decode_huffman(const EncodedBlock& block, std::vector<uint8_t>& out);

    void set_codes(const CodeLengths& lengths);

    uint8_t block_size_log2_;
    Frequencies frequencies_{}; // zero after decode: the format stores no frequencies
    Codes codes_{};
};

} // namespace compressor

#endif /* Engine_h */
