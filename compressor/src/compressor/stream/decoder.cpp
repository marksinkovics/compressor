#include <compressor/stream/decoder.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <future>
#include <ios>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/longrange/longrange.h>
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
    // A references block's, which become `bytes` once the blocks before it are decoded.
    std::vector<Reference> references;
    std::vector<uint8_t> literals;
};

// Everything of a block that doesn't need the blocks before it.
void decode_alone(IEngine& engine, Decoded& task)
{
    task.bytes.clear();
    task.references.clear();
    task.literals.clear();
    if (task.block.method_ != Method::references)
    {
        task.result = engine.decode_block(task.block, task.bytes);
        return;
    }
    EncodedBlock inner;
    task.result = read_references(task.block.payload_, task.references, inner);
    if (task.result)
    {
        task.result = engine.decode_block(inner, task.literals);
    }
}

class HistorySource : public BlockDecoder::LiteralSource
{
public:
    explicit HistorySource(uint64_t window_blocks)
        : history_(window_blocks)
    {
    }

    std::expected<std::span<const uint8_t>, Error> earlier(uint32_t back) override
    {
        return history_.earlier(back);
    }

    void add(std::vector<uint8_t> literals) override
    {
        history_.add(std::move(literals));
    }

private:
    LongRangeHistory history_;
};

// For decoding blocks from the middle of a frame: the blocks before the first one are read and
// decoded when a copy needs them.
class FetchingSource : public BlockDecoder::LiteralSource
{
public:
    using DecodeAlone = std::function<Decoded(Decoded)>;

    FetchingSource(std::istream& in, const FrameBlocks& blocks, std::size_t first,
                   DecodeAlone decode)
        : in_(in)
        , blocks_(blocks)
        , first_(first)
        , window_blocks_(window_blocks(blocks.block_size_log2, blocks.window_log2))
        , history_(window_blocks_)
        , decode_(std::move(decode))
    {
    }

    std::expected<std::span<const uint8_t>, Error> earlier(uint32_t back) override
    {
        if (back <= added_)
        {
            return history_.earlier(back);
        }
        if (back > window_blocks_ || back - added_ > first_)
        {
            return std::unexpected(Error::corrupt_payload);
        }
        return fetch(first_ - (back - added_));
    }

    void add(std::vector<uint8_t> literals) override
    {
        history_.add(std::move(literals));
        ++added_;
    }

private:
    std::expected<std::span<const uint8_t>, Error> fetch(std::size_t index)
    {
        if (const auto found = fetched_.find(index); found != fetched_.end())
        {
            return std::span<const uint8_t>(found->second);
        }
        const std::streampos resume = in_.tellg();
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(blocks_.offsets[index]));
        FrameReader reader(in_);
        reader.start_blocks(blocks_.block_size_log2, blocks_.flags);
        Decoded task;
        const auto read = reader.read_block(task.block);
        const bool ends_right =
            std::cmp_equal(static_cast<std::streamoff>(in_.tellg()), blocks_.offsets[index + 1]);
        in_.clear();
        in_.seekg(resume);
        if (!read)
        {
            return std::unexpected(read.error());
        }
        if (!*read || !ends_right)
        {
            return std::unexpected(Error::corrupt_index);
        }
        task = decode_(std::move(task));
        if (!task.result)
        {
            return std::unexpected(task.result.error());
        }
        std::vector<uint8_t>& literals = fetched_[index];
        literals = task.block.method_ == Method::references ? std::move(task.literals)
                                                            : std::move(task.bytes);
        return std::span<const uint8_t>(literals);
    }

    std::istream& in_;
    const FrameBlocks& blocks_;
    std::size_t first_;
    uint64_t window_blocks_;
    LongRangeHistory history_;
    uint64_t added_ = 0;
    std::map<std::size_t, std::vector<uint8_t>> fetched_;
    DecodeAlone decode_;
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
    const auto header = reader.read_header();
    if (!header)
    {
        return std::unexpected(header.error());
    }
    // A search index changes nothing about the blocks.
    if ((reader.flags() & archive_flag) != (flags & archive_flag))
    {
        return std::unexpected((reader.flags() & archive_flag) != 0 ? Error::unexpected_archive
                                                                    : Error::corrupt_header);
    }

    std::optional<HistorySource> history;
    if ((reader.flags() & long_range_flag) != 0)
    {
        history.emplace(window_blocks(*header, reader.window_log2()));
    }
    return decode_all([&reader](EncodedBlock& block) {
        return reader.read_block(block);
    }, out, history ? &*history : nullptr);
}

std::expected<void, Error> BlockDecoder::decode_blocks(std::istream& in, std::ostream& out,
                                                       const FrameBlocks& blocks, std::size_t first,
                                                       std::size_t end)
{
    if (first >= end)
    {
        return {};
    }
    if (end >= blocks.offsets.size())
    {
        return std::unexpected(Error::corrupt_index);
    }
    in.clear();
    in.seekg(static_cast<std::streamoff>(blocks.offsets[first]));
    FrameReader reader(in);
    reader.start_blocks(blocks.block_size_log2, blocks.flags);
    std::optional<FetchingSource> fetching;
    if ((blocks.flags & long_range_flag) != 0)
    {
        fetching.emplace(in, blocks, first, [this](Decoded task) {
            return pool_
                .submit([&task, this](std::size_t worker) {
                decode_alone(*engines_[worker], task);
                return std::move(task);
            }).get();
        });
    }
    std::size_t next = first;
    return decode_all([&](EncodedBlock& block) -> std::expected<bool, Error> {
        if (std::cmp_not_equal(static_cast<std::streamoff>(in.tellg()), blocks.offsets[next]))
        {
            return std::unexpected(Error::corrupt_index);
        }
        if (next == end)
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
    }, out, fetching ? &*fetching : nullptr);
}

std::expected<void, Error> BlockDecoder::decode_all(const NextBlock& next_block, std::ostream& out,
                                                    LiteralSource* literals)
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
        if (literals != nullptr)
        {
            if (decoded.block.method_ == Method::references)
            {
                if (auto rebuilt =
                        rebuild_block(decoded.block, decoded.references, decoded.literals,
                                      [literals](uint32_t back) {
                    return literals->earlier(back);
                }, decoded.bytes);
                    !rebuilt)
                {
                    return rebuilt;
                }
                literals->add(std::exchange(decoded.literals, {}));
            }
            else
            {
                literals->add(decoded.bytes);
            }
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
            decode_alone(*engines_[worker], task);
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
