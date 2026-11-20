#ifndef Engine_h
#define Engine_h

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <iosfwd>
#include <memory>
#include <span>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/dictionary/dictionary.h>
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

// Graphviz dot of the code tree, each node labelled with its symbols, frequency and code prefix.
void write_code_graph(std::ostream& out, const Codes& codes, const Frequencies& frequencies);

// The level from which Engine tries every transform choose_transforms() finds plausible.
inline constexpr unsigned auto_level = 7;
// The level from which the BWT and LZR coders compete with LZ77 + Huffman.
inline constexpr unsigned bwt_level = 8;
// The level at which the context-mixing coder competes too, without a transform and with the best
// pipeline's.
inline constexpr unsigned cm_level = 9;

// Makes one engine per worker thread.
using EngineFactory = std::function<std::unique_ptr<IEngine>()>;

// At least one.
std::vector<std::unique_ptr<IEngine>> make_engines(const EngineFactory& make_engine,
                                                   unsigned count);

class Engine : public IEngine
{
public:
    // Level 1 chooses between stored and Huffman; from 2 on, an LZ77 + Huffman pipeline block
    // competes too, with longer match searches the higher the level: without a transform and with
    // the one choose_transforms() picks, or from auto_level on with each of its candidates.
    // With a `dictionary`, the LZ77 + Huffman and LZR coders match into it (coders 9 and 10),
    // and blocks coded with it can be decoded.
    explicit Engine(uint8_t block_size_log2 = default_block_size_log2, unsigned level = 1,
                    std::shared_ptr<const Dictionary> dictionary = nullptr);

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
    std::expected<void, Error> decode_pipeline(const EncodedBlock& block,
                                               std::vector<uint8_t>& out) const;
    // The dictionary a coder 9 or 10 payload names, checked against ours.
    std::expected<std::span<const uint8_t>, Error>
    dictionary_for(std::span<const uint8_t> payload) const;

    void set_codes(const CodeLengths& lengths);

    uint8_t block_size_log2_;
    unsigned level_;
    std::shared_ptr<const Dictionary> dictionary_;
    Frequencies frequencies_{}; // zero after decode: the format stores no frequencies
    Codes codes_{};
};

} // namespace compressor

#endif /* Engine_h */
