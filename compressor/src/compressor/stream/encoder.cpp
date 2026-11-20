#include <compressor/stream/encoder.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <future>
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

// The input comes back with the result so its buffer can be reused.
struct Encoded
{
    EncodedBlock block;
    std::vector<uint8_t> input;
};

} // namespace

BlockEncoder::BlockEncoder()
    : BlockEncoder([] {
        return std::make_unique<Engine>();
    }, default_block_size_log2, default_thread_count())
{
}

BlockEncoder::BlockEncoder(const EngineFactory& make_engine, uint8_t block_size_log2,
                           unsigned threads)
    : engines_(make_engines(make_engine, threads))
    , block_size_log2_(block_size_log2)
    , pool_(static_cast<unsigned>(engines_.size()))
{
}

BlockEncoder::BlockEncoder(std::unique_ptr<IEngine> engine, uint8_t block_size_log2)
    : engines_(one_engine(std::move(engine)))
    , block_size_log2_(block_size_log2)
    , pool_(1)
{
}

std::expected<void, Error> BlockEncoder::encode(std::istream& in, std::ostream& out)
{
    const std::size_t block_size = std::size_t{1} << block_size_log2_;
    const std::size_t max_in_flight = 2 * pool_.size();
    write_frame_header(out, block_size_log2_);

    // Tasks left in flight after an early return still finish safely: they own their input, and
    // only this worker's engine touches it.
    std::deque<std::future<Encoded>> in_flight;
    std::vector<std::vector<uint8_t>> spare_buffers;
    const auto write_oldest = [&] {
        Encoded encoded = in_flight.front().get();
        in_flight.pop_front();
        {
            PROFILE_SCOPE("Write")
            write_block(out, encoded.block);
        }
        spare_buffers.push_back(std::move(encoded.input));
        return static_cast<bool>(out);
    };

    uint64_t total = 0;
    while (true)
    {
        std::vector<uint8_t> buffer;
        if (!spare_buffers.empty())
        {
            buffer = std::move(spare_buffers.back());
            spare_buffers.pop_back();
        }
        buffer.resize(block_size);
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
        buffer.resize(got);
        if (in_flight.size() >= max_in_flight && !write_oldest())
        {
            return std::unexpected(Error::write_failed);
        }
        in_flight.push_back(
            pool_.submit([this, input = std::move(buffer)](std::size_t worker) mutable {
            EncodedBlock block = engines_[worker]->encode_block(input);
            return Encoded{.block = std::move(block), .input = std::move(input)};
        }));
        total += got;
        // A short read means end of input; reading again would block on a pipe.
        if (got < block_size)
        {
            break;
        }
    }
    while (!in_flight.empty())
    {
        if (!write_oldest())
        {
            return std::unexpected(Error::write_failed);
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
