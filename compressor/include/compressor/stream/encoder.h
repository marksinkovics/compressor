#ifndef BlockEncoder_h
#define BlockEncoder_h

#include <cstdint>
#include <expected>
#include <iosfwd>
#include <memory>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

namespace compressor
{

class IBlockEncoder
{
public:
    virtual ~IBlockEncoder() = default;
    // Writes the whole container for everything left in `in`.
    virtual std::expected<void, Error> encode(std::istream& in, std::ostream& out) = 0;
};

// Holds one input block and one encoded block at a time.
class BlockEncoder : public IBlockEncoder
{
public:
    BlockEncoder();
    explicit BlockEncoder(std::unique_ptr<IEngine> engine,
                          uint8_t block_size_log2 = default_block_size_log2);
    ~BlockEncoder() override = default;

    std::expected<void, Error> encode(std::istream& in, std::ostream& out) override;

private:
    std::unique_ptr<IEngine> engine_;
    uint8_t block_size_log2_;
};

} // namespace compressor

#endif /* BlockEncoder_h */
