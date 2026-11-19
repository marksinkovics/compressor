#include <compressor/stream/encoder.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <memory>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

BlockEncoder::BlockEncoder()
    : BlockEncoder(std::make_unique<Engine>())
{
}

BlockEncoder::BlockEncoder(std::unique_ptr<IEngine> engine, uint8_t block_size_log2)
    : engine_(std::move(engine))
    , block_size_log2_(block_size_log2)
{
}

std::expected<void, Error> BlockEncoder::encode(std::istream& in, std::ostream& out)
{
    const std::size_t block_size = std::size_t{1} << block_size_log2_;
    std::vector<uint8_t> buffer(block_size);
    write_frame_header(out, block_size_log2_);

    uint64_t total = 0;
    while (true)
    {
        std::size_t got = 0;
        {
            PROFILE_SCOPE("Read")
            in.read(reinterpret_cast<char*>(buffer.data()),
                    static_cast<std::streamsize>(buffer.size()));
            got = static_cast<std::size_t>(in.gcount());
        }
        if (in.bad())
        {
            return std::unexpected(Error::read_failed);
        }
        if (got == 0)
        {
            break;
        }
        const EncodedBlock block = engine_->encode_block(std::span(buffer).first(got));
        {
            PROFILE_SCOPE("Write")
            write_block(out, block);
        }
        if (!out)
        {
            return std::unexpected(Error::write_failed);
        }
        total += got;
        // A short read means end of input; reading again would block on a pipe.
        if (got < block_size)
        {
            break;
        }
    }

    write_end_block(out, total);
    out.flush();
    if (!out)
    {
        return std::unexpected(Error::write_failed);
    }
    return {};
}

} // namespace compressor
