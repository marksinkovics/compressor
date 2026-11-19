#ifndef BlockDecoder_h
#define BlockDecoder_h

#include <expected>
#include <iosfwd>
#include <memory>

#include <compressor/engine/engine.h>
#include <compressor/error.h>

namespace compressor
{

class IBlockDecoder
{
public:
    virtual ~IBlockDecoder() = default;
    // Writes each block as soon as it is verified, so on failure `out` may hold earlier blocks.
    virtual std::expected<void, Error> decode(std::istream& in, std::ostream& out) = 0;
};

// Holds one encoded block and one decoded block at a time.
class BlockDecoder : public IBlockDecoder
{
public:
    BlockDecoder();
    explicit BlockDecoder(std::unique_ptr<IEngine> engine);
    ~BlockDecoder() override = default;

    std::expected<void, Error> decode(std::istream& in, std::ostream& out) override;

private:
    std::unique_ptr<IEngine> engine_;
};

} // namespace compressor

#endif /* BlockDecoder_h */
