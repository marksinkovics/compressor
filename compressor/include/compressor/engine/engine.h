#ifndef Engine_h
#define Engine_h

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
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

    // Uses whichever of stored and Huffman gives the smaller block.
    virtual EncodedBlock encode_block(std::span<const uint8_t> bytes) = 0;
    // Appends the decoded bytes to `out`. Fails unless a stored payload is block.size_ bytes, or a
    // Huffman payload holds exactly block.size_ codes followed by zero padding, and the bytes
    // match block.crc_.
    virtual std::expected<void, Error> decode_block(const EncodedBlock& block,
                                                    std::vector<uint8_t>& out) = 0;
};

// Makes one engine per worker thread.
using EngineFactory = std::function<std::unique_ptr<IEngine>()>;

// At least one.
std::vector<std::unique_ptr<IEngine>> make_engines(const EngineFactory& make_engine,
                                                   unsigned count);

class Engine : public IEngine
{
public:
    explicit Engine(uint8_t block_size_log2 = default_block_size_log2);

    // Graphviz dot of the code tree of the last block encoded or decoded, rebuilt from the codes.
    void print_graph() const override;
    void print_dict() const override;

    EncodedBlock encode_block(std::span<const uint8_t> bytes) override;
    std::expected<void, Error> decode_block(const EncodedBlock& block,
                                            std::vector<uint8_t>& out) override;

    // Whole buffers in memory, for tests and benchmarks; split into blocks of the engine's size.
    EncodedData encode(const DecodedData& data);
    std::expected<DecodedData, Error> decode(const EncodedData& data);

private:
    std::expected<void, Error> decode_huffman(const EncodedBlock& block, std::vector<uint8_t>& out);

    void set_codes(const CodeLengths& lengths);

    uint8_t block_size_log2_;
    Frequencies frequencies_{}; // zero after decode: the format stores no frequencies
    Codes codes_{};
};

} // namespace compressor

#endif /* Engine_h */
