#ifndef BlockDecoder_h
#define BlockDecoder_h

#include <cstdint>
#include <expected>
#include <functional>
#include <iosfwd>
#include <memory>
#include <span>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/longrange/longrange.h>
#include <compressor/thread/pool.h>

namespace compressor
{

// A container's blocks, for decoding some of them.
struct FrameBlocks
{
    std::span<const uint64_t> offsets; // where each block starts, then where the last one ends
    uint8_t block_size_log2 = default_block_size_log2;
    uint16_t flags = 0;      // long_range_flag: blocks may copy from earlier ones
    uint8_t window_log2 = 0; // with long_range_flag
};

class IBlockDecoder
{
public:
    virtual ~IBlockDecoder() = default;
    // Writes each block as soon as it and every block before it are verified, so on failure `out`
    // holds exactly the blocks before the first bad one.
    virtual std::expected<void, Error> decode(std::istream& in, std::ostream& out) = 0;
    // decode() for a container whose archive flag is as in `flags`. An archive's stream is left
    // just after its end block.
    virtual std::expected<void, Error> decode_frame(std::istream& in, std::ostream& out,
                                                    uint16_t flags) = 0;
    // Decodes blocks `first` to `end` - 1; each must end where the next one starts. A block that
    // copies from blocks before `first` has them read and decoded too. Leaves the stream at
    // blocks.offsets[end].
    virtual std::expected<void, Error> decode_blocks(std::istream& in, std::ostream& out,
                                                     const FrameBlocks& blocks, std::size_t first,
                                                     std::size_t end) = 0;
};

// Decodes up to 2 blocks per thread at once and writes them in order.
class BlockDecoder : public IBlockDecoder
{
public:
    // Engine and default_thread_count() threads.
    BlockDecoder();
    BlockDecoder(const EngineFactory& make_engine, unsigned threads);
    // One thread.
    explicit BlockDecoder(std::unique_ptr<IEngine> engine);
    ~BlockDecoder() override = default;
    BlockDecoder(const BlockDecoder&) = delete;
    BlockDecoder& operator=(const BlockDecoder&) = delete;
    BlockDecoder(BlockDecoder&&) = delete;
    BlockDecoder& operator=(BlockDecoder&&) = delete;

    std::expected<void, Error> decode(std::istream& in, std::ostream& out) override;
    std::expected<void, Error> decode_frame(std::istream& in, std::ostream& out,
                                            uint16_t flags) override;
    std::expected<void, Error> decode_blocks(std::istream& in, std::ostream& out,
                                             const FrameBlocks& blocks, std::size_t first,
                                             std::size_t end) override;

    // Where a long-range frame's earlier literals come from while its blocks are decoded in order.
    class LiteralSource
    {
    public:
        virtual ~LiteralSource() = default;
        virtual std::expected<std::span<const uint8_t>, Error> earlier(uint32_t back) = 0;
        virtual void add(std::vector<uint8_t> literals) = 0;
    };

private:
    using NextBlock = std::function<std::expected<bool, Error>(EncodedBlock&)>;

    // Decodes and writes blocks until `next` returns false; `literals` for a long-range frame.
    std::expected<void, Error> decode_all(const NextBlock& next, std::ostream& out,
                                          LiteralSource* literals);

    std::vector<std::unique_ptr<IEngine>> engines_; // one per worker
    ThreadPool pool_; // after engines_, so it finishes its tasks before they are destroyed
};

} // namespace compressor

#endif /* BlockDecoder_h */
