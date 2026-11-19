#include <compressor/stream/decoder.h>

#include <cstdint>
#include <expected>
#include <istream>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

BlockDecoder::BlockDecoder()
    : BlockDecoder(std::make_unique<Engine>())
{
}

BlockDecoder::BlockDecoder(std::unique_ptr<IEngine> engine)
    : engine_(std::move(engine))
{
}

std::expected<void, Error> BlockDecoder::decode(std::istream& in, std::ostream& out)
{
    FrameReader reader(in);
    if (auto header = reader.read_header(); !header)
    {
        return std::unexpected(header.error());
    }

    EncodedBlock block;
    std::vector<uint8_t> decoded;
    while (true)
    {
        std::expected<bool, Error> more;
        {
            PROFILE_SCOPE("Read")
            more = reader.read_block(block);
        }
        if (!more)
        {
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            break;
        }
        decoded.clear();
        if (auto result = engine_->decode_block(block, decoded); !result)
        {
            return result;
        }
        {
            PROFILE_SCOPE("Write")
            out.write(reinterpret_cast<const char*>(decoded.data()),
                      static_cast<std::streamsize>(decoded.size()));
        }
        if (!out)
        {
            return std::unexpected(Error::write_failed);
        }
    }

    out.flush();
    if (!out)
    {
        return std::unexpected(Error::write_failed);
    }
    return {};
}

} // namespace compressor
