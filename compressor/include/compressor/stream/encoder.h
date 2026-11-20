#ifndef BlockEncoder_h
#define BlockEncoder_h

#include <cstdint>
#include <expected>
#include <iosfwd>
#include <memory>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/thread/pool.h>

namespace compressor
{

class IBlockEncoder
{
public:
    virtual ~IBlockEncoder() = default;
    // Writes the whole container for everything left in `in`.
    virtual std::expected<void, Error> encode(std::istream& in, std::ostream& out) = 0;
};

// Encodes up to 2 blocks per thread at once and writes them in input order, so the output doesn't
// depend on the thread count.
class BlockEncoder : public IBlockEncoder
{
public:
    // Engine, the default block size and default_thread_count() threads.
    BlockEncoder();
    BlockEncoder(const EngineFactory& make_engine, uint8_t block_size_log2, unsigned threads);
    // One thread.
    explicit BlockEncoder(std::unique_ptr<IEngine> engine,
                          uint8_t block_size_log2 = default_block_size_log2);
    ~BlockEncoder() override = default;
    BlockEncoder(const BlockEncoder&) = delete;
    BlockEncoder& operator=(const BlockEncoder&) = delete;
    BlockEncoder(BlockEncoder&&) = delete;
    BlockEncoder& operator=(BlockEncoder&&) = delete;

    std::expected<void, Error> encode(std::istream& in, std::ostream& out) override;

private:
    std::vector<std::unique_ptr<IEngine>> engines_; // one per worker
    uint8_t block_size_log2_;
    ThreadPool pool_; // after engines_, so it finishes its tasks before they are destroyed
};

} // namespace compressor

#endif /* BlockEncoder_h */
