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
#include <compressor/thread/pool.h>

namespace compressor
{

class IBlockDecoder
{
public:
    virtual ~IBlockDecoder() = default;
    // Writes each block as soon as it and every block before it are verified, so on failure `out`
    // holds exactly the blocks before the first bad one.
    virtual std::expected<void, Error> decode(std::istream& in, std::ostream& out) = 0;
    // decode() for a container whose header flags are exactly `flags`. An archive's stream is
    // left just after its end block.
    virtual std::expected<void, Error> decode_frame(std::istream& in, std::ostream& out,
                                                    uint16_t flags) = 0;
    // Decodes the blocks of an archive's block table that start at each of `offsets` but the last,
    // which is where the last block must end; each block must end where the next one starts.
    // Leaves the stream at offsets.back().
    virtual std::expected<void, Error> decode_blocks(std::istream& in, std::ostream& out,
                                                     uint8_t block_size_log2,
                                                     std::span<const uint64_t> offsets) = 0;
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
                                             uint8_t block_size_log2,
                                             std::span<const uint64_t> offsets) override;

private:
    using NextBlock = std::function<std::expected<bool, Error>(EncodedBlock&)>;

    // Decodes and writes blocks until `next` returns false.
    std::expected<void, Error> decode_all(const NextBlock& next, std::ostream& out);

    std::vector<std::unique_ptr<IEngine>> engines_; // one per worker
    ThreadPool pool_; // after engines_, so it finishes its tasks before they are destroyed
};

} // namespace compressor

#endif /* BlockDecoder_h */
