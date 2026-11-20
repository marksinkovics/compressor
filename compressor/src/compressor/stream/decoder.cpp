#include <compressor/stream/decoder.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <future>
#include <ios>
#include <istream>
#include <memory>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/thread/pool.h>

#include <instrumentor/instrumentor.h>

namespace compressor
{

namespace
{

std::vector<std::unique_ptr<IEngine>> one_engine(std::unique_ptr<IEngine> engine)
{
    std::vector<std::unique_ptr<IEngine>> engines;
    engines.push_back(std::move(engine));
    return engines;
}

// The block comes back with the result so its buffers can be reused.
struct Decoded
{
    std::expected<void, Error> result;
    EncodedBlock block;
    std::vector<uint8_t> bytes;
};

} // namespace

BlockDecoder::BlockDecoder()
    : BlockDecoder([] {
        return std::make_unique<Engine>();
    }, default_thread_count())
{
}

BlockDecoder::BlockDecoder(const EngineFactory& make_engine, unsigned threads)
    : engines_(make_engines(make_engine, threads))
    , pool_(static_cast<unsigned>(engines_.size()))
{
}

BlockDecoder::BlockDecoder(std::unique_ptr<IEngine> engine)
    : engines_(one_engine(std::move(engine)))
    , pool_(1)
{
}

std::expected<void, Error> BlockDecoder::decode(std::istream& in, std::ostream& out)
{
    return decode_frame(in, out, 0);
}

std::expected<void, Error> BlockDecoder::decode_frame(std::istream& in, std::ostream& out,
                                                      uint16_t flags)
{
    FrameReader reader(in);
    if (auto header = reader.read_header(); !header)
    {
        return std::unexpected(header.error());
    }
    // A search index changes nothing about the blocks.
    if ((reader.flags() & archive_flag) != (flags & archive_flag))
    {
        return std::unexpected((reader.flags() & archive_flag) != 0 ? Error::unexpected_archive
                                                                    : Error::corrupt_header);
    }

    return decode_all([&reader](EncodedBlock& block) {
        return reader.read_block(block);
    }, out);
}

std::expected<void, Error> BlockDecoder::decode_blocks(std::istream& in, std::ostream& out,
                                                       uint8_t block_size_log2,
                                                       std::span<const uint64_t> offsets)
{
    if (offsets.empty())
    {
        return {};
    }
    in.clear();
    in.seekg(static_cast<std::streamoff>(offsets.front()));
    FrameReader reader(in);
    reader.start_blocks(block_size_log2);
    std::size_t next = 0;
    return decode_all([&](EncodedBlock& block) -> std::expected<bool, Error> {
        if (std::cmp_not_equal(static_cast<std::streamoff>(in.tellg()), offsets[next]))
        {
            return std::unexpected(Error::corrupt_index);
        }
        if (next + 1 == offsets.size())
        {
            return false;
        }
        ++next;
        auto more = reader.read_block(block);
        if (more && !*more)
        {
            return std::unexpected(Error::corrupt_index);
        }
        return more;
    }, out);
}

std::expected<void, Error> BlockDecoder::decode_all(const NextBlock& next_block, std::ostream& out)
{
    const std::size_t max_in_flight = 2 * pool_.size();
    // Tasks left in flight after an early return still finish safely: they own their block, and
    // only this worker's engine touches it.
    std::deque<std::future<Decoded>> in_flight;
    std::vector<Decoded> spares;
    const auto write_oldest = [&]() -> std::expected<void, Error> {
        Decoded decoded = in_flight.front().get();
        in_flight.pop_front();
        if (!decoded.result)
        {
            return decoded.result;
        }
        {
            PROFILE_SCOPE("Write")
            out.write(reinterpret_cast<const char*>(decoded.bytes.data()),
                      static_cast<std::streamsize>(decoded.bytes.size()));
        }
        spares.push_back(std::move(decoded));
        if (!out)
        {
            return std::unexpected(Error::write_failed);
        }
        return {};
    };
    const auto write_all = [&]() -> std::expected<void, Error> {
        while (!in_flight.empty())
        {
            if (auto written = write_oldest(); !written)
            {
                return written;
            }
        }
        return {};
    };

    while (true)
    {
        Decoded next;
        if (!spares.empty())
        {
            next = std::move(spares.back());
            spares.pop_back();
        }
        std::expected<bool, Error> more;
        {
            PROFILE_SCOPE("Read")
            more = next_block(next.block);
        }
        if (!more)
        {
            // The blocks before a bad one are written first, as when decoding one at a time.
            if (auto written = write_all(); !written)
            {
                return written;
            }
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            break;
        }
        if (in_flight.size() >= max_in_flight)
        {
            if (auto written = write_oldest(); !written)
            {
                return written;
            }
        }
        in_flight.push_back(
            pool_.submit([this, task = std::move(next)](std::size_t worker) mutable {
            task.bytes.clear();
            task.result = engines_[worker]->decode_block(task.block, task.bytes);
            return std::move(task);
        }));
    }
    if (auto written = write_all(); !written)
    {
        return written;
    }

    out.flush();
    if (!out)
    {
        return std::unexpected(Error::write_failed);
    }
    return {};
}

} // namespace compressor
