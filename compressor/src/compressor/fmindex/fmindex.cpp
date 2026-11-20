#include <compressor/fmindex/fmindex.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <future>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>

#include <compressor/bwt/bwt.h>
#include <compressor/checksum/crc32.h>
#include <compressor/error.h>
#include <compressor/thread/pool.h>

namespace compressor
{

namespace
{

constexpr uint8_t section_version = 1;
constexpr std::size_t rank_interval = 256;

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_index);
}

// The builder cuts a full chunk (chunk + overlap bytes) whenever that much is pending, then keeps
// the overlap; whatever is left at the end, at least the overlap, is the last chunk.
uint64_t full_chunks(uint64_t total)
{
    return total >= uint64_t{fm_chunk_bytes} + fm_overlap ? (total - fm_overlap) / fm_chunk_bytes
                                                          : 0;
}

void put_le(std::vector<uint8_t>& out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
    {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

class Reader
{
public:
    explicit Reader(std::span<const uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    bool get(std::size_t size, uint64_t& value)
    {
        if (bytes_.size() - at_ < size)
        {
            return false;
        }
        value = 0;
        for (std::size_t i = 0; i < size; ++i)
        {
            value |= uint64_t{bytes_[at_ + i]} << (8 * i);
        }
        at_ += size;
        return true;
    }

    bool take(std::size_t size, std::span<const uint8_t>& out)
    {
        if (bytes_.size() - at_ < size)
        {
            return false;
        }
        out = bytes_.subspan(at_, size);
        at_ += size;
        return true;
    }

    bool done() const
    {
        return at_ == bytes_.size();
    }

private:
    std::span<const uint8_t> bytes_;
    std::size_t at_ = 0;
};

// One chunk ready to search: its BWT with the end marker's row left out, rank checkpoints, and the
// sampled positions.
class Chunk
{
public:
    std::expected<void, Error> load(std::span<const uint8_t> serialised, uint32_t& length)
    {
        Reader reader(serialised);
        uint64_t size = 0;
        uint64_t payload_size = 0;
        std::span<const uint8_t> payload;
        if (!reader.get(4, size) || !reader.get(4, payload_size) ||
            size > uint64_t{fm_chunk_bytes} + fm_overlap || !reader.take(payload_size, payload))
        {
            return corrupt();
        }
        n_ = static_cast<std::size_t>(size);
        length = static_cast<uint32_t>(n_);
        const auto primary = bwt_decode_transformed(payload, transformed_, n_);
        if (!primary)
        {
            return corrupt();
        }
        primary_ = *primary;

        // Rows 0..n: row 0 is the end marker's, so it sorts first.
        std::array<uint32_t, 256> counts{};
        checkpoints_.assign((n_ / rank_interval) + 1, {});
        for (std::size_t i = 0; i < n_; ++i)
        {
            if (i % rank_interval == 0)
            {
                checkpoints_[i / rank_interval] = counts;
            }
            ++counts[transformed_[i]];
        }
        if (n_ % rank_interval == 0)
        {
            checkpoints_[n_ / rank_interval] = counts;
        }
        uint32_t next = 1;
        for (std::size_t c = 0; c < 256; ++c)
        {
            first_[c] = next;
            next += counts[c];
        }

        std::span<const uint8_t> bits;
        uint64_t sample_count = 0;
        if (!reader.take(((n_ + 1) + 7) / 8, bits) || !reader.get(4, sample_count))
        {
            return corrupt();
        }
        words_.assign(((n_ + 1) + 63) / 64, 0);
        for (std::size_t i = 0; i < bits.size(); ++i)
        {
            words_[i / 8] |= uint64_t{bits[i]} << (8 * (i % 8));
        }
        word_ranks_.assign(words_.size() + 1, 0);
        for (std::size_t w = 0; w < words_.size(); ++w)
        {
            word_ranks_[w + 1] = word_ranks_[w] + static_cast<uint32_t>(std::popcount(words_[w]));
        }
        if (word_ranks_.back() != sample_count)
        {
            return corrupt();
        }
        samples_.resize(sample_count);
        for (uint32_t& sample : samples_)
        {
            uint64_t value = 0;
            if (!reader.get(4, value) || value > n_)
            {
                return corrupt();
            }
            sample = static_cast<uint32_t>(value);
        }
        if (!reader.done())
        {
            return corrupt();
        }
        return {};
    }

    // Text positions where `pattern` starts within this chunk.
    std::expected<std::vector<uint32_t>, Error> find(std::string_view pattern) const
    {
        uint32_t begin = 0;
        auto end = static_cast<uint32_t>(n_ + 1);
        for (std::size_t k = pattern.size(); k-- > 0 && begin < end;)
        {
            const auto c = static_cast<uint8_t>(pattern[k]);
            begin = first_[c] + rank(c, begin);
            end = first_[c] + rank(c, end);
        }
        std::vector<uint32_t> positions;
        for (uint32_t row = begin; row < end; ++row)
        {
            const auto position = locate(row);
            if (!position)
            {
                return std::unexpected(position.error());
            }
            positions.push_back(*position);
        }
        std::ranges::sort(positions);
        return positions;
    }

private:
    // Occurrences of `c` in the last column above `row`.
    uint32_t rank(uint8_t c, uint32_t row) const
    {
        const std::size_t t = row <= primary_ ? row : row - 1;
        const std::size_t base = t / rank_interval;
        uint32_t count = checkpoints_[base][c];
        for (std::size_t i = base * rank_interval; i < t; ++i)
        {
            count += static_cast<uint32_t>(transformed_[i] == c);
        }
        return count;
    }

    bool sampled(uint32_t row, uint32_t& value) const
    {
        const uint64_t word = words_[row / 64];
        const uint64_t bit = uint64_t{1} << (row % 64);
        if ((word & bit) == 0)
        {
            return false;
        }
        value = samples_[word_ranks_[row / 64] +
                         static_cast<uint32_t>(std::popcount(word & (bit - 1)))];
        return true;
    }

    // Walks LF back to a sampled row; each step is one position earlier in the text.
    std::expected<uint32_t, Error> locate(uint32_t row) const
    {
        for (uint32_t steps = 0; steps <= fm_sample_rate; ++steps)
        {
            uint32_t value = 0;
            if (sampled(row, value))
            {
                return value + steps;
            }
            if (row == primary_)
            {
                return steps; // the suffix starting at 0
            }
            const uint8_t c = transformed_[row < primary_ ? row : row - 1];
            row = first_[c] + rank(c, row);
        }
        return corrupt();
    }

    std::size_t n_ = 0;
    std::vector<uint8_t> transformed_;
    uint32_t primary_ = 0;
    std::array<uint32_t, 256> first_{};
    std::vector<std::array<uint32_t, 256>> checkpoints_;
    std::vector<uint64_t> words_;
    std::vector<uint32_t> word_ranks_;
    std::vector<uint32_t> samples_;
};

} // namespace

void FmIndexBuilder::add(std::span<const uint8_t> bytes)
{
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    while (pending_.size() >= std::size_t{fm_chunk_bytes} + fm_overlap)
    {
        build_chunk(std::span(pending_).first(std::size_t{fm_chunk_bytes} + fm_overlap));
        pending_.erase(pending_.begin(), pending_.begin() + fm_chunk_bytes);
    }
}

void FmIndexBuilder::build_chunk(std::span<const uint8_t> data)
{
    const std::size_t n = data.size();
    const std::vector<uint32_t> sa = suffix_array(data);
    std::vector<uint8_t> transformed;
    transformed.reserve(n);
    uint32_t primary = 0;
    if (n > 0)
    {
        transformed.push_back(data[n - 1]);
    }
    std::vector<uint8_t> bits(((n + 1) + 7) / 8);
    std::vector<uint32_t> samples;
    const auto mark = [&](std::size_t row, uint32_t position) {
        if (position % fm_sample_rate == 0)
        {
            bits[row / 8] |= static_cast<uint8_t>(1U << (row % 8));
            samples.push_back(position);
        }
    };
    mark(0, static_cast<uint32_t>(n));
    for (std::size_t row = 1; row <= n; ++row)
    {
        const uint32_t position = sa[row - 1];
        mark(row, position);
        if (position == 0)
        {
            primary = static_cast<uint32_t>(row);
            continue;
        }
        transformed.push_back(data[position - 1]);
    }
    const std::vector<uint8_t> payload = bwt_encode_transformed(transformed, primary);

    std::vector<uint8_t> chunk;
    put_le(chunk, n, 4);
    put_le(chunk, payload.size(), 4);
    chunk.insert(chunk.end(), payload.begin(), payload.end());
    chunk.insert(chunk.end(), bits.begin(), bits.end());
    put_le(chunk, samples.size(), 4);
    for (const uint32_t sample : samples)
    {
        put_le(chunk, sample, 4);
    }
    chunks_.push_back(std::move(chunk));
}

void FmIndexBuilder::write(std::ostream& out, uint64_t section_offset)
{
    if (!pending_.empty() || chunks_.empty())
    {
        build_chunk(pending_);
        pending_.clear();
    }
    std::vector<uint8_t> section{section_version};
    put_le(section, fm_chunk_bytes, 4);
    put_le(section, fm_overlap, 4);
    put_le(section, fm_sample_rate, 4);
    put_le(section, chunks_.size(), 4);
    for (const std::vector<uint8_t>& chunk : chunks_)
    {
        put_le(section, chunk.size(), 8);
        section.insert(section.end(), chunk.begin(), chunk.end());
    }
    std::vector<uint8_t> trailer;
    put_le(trailer, section_offset, 8);
    put_le(trailer, crc32(section), 4);
    trailer.insert(trailer.end(), search_index_magic.begin(), search_index_magic.end());
    out.write(reinterpret_cast<const char*>(section.data()),
              static_cast<std::streamsize>(section.size()));
    out.write(reinterpret_cast<const char*>(trailer.data()),
              static_cast<std::streamsize>(trailer.size()));
}

std::expected<FmIndex, Error> read_search_index(std::istream& in, uint64_t section_offset,
                                                uint64_t section_end, uint64_t total_size)
{
    if (section_end < section_offset)
    {
        return corrupt();
    }
    std::vector<uint8_t> section(section_end - section_offset);
    in.clear();
    in.seekg(static_cast<std::streamoff>(section_offset));
    in.read(reinterpret_cast<char*>(section.data()), static_cast<std::streamsize>(section.size()));
    if (static_cast<std::size_t>(in.gcount()) != section.size())
    {
        return std::unexpected(in.bad() ? Error::read_failed : Error::truncated);
    }
    std::array<uint8_t, search_trailer_bytes> trailer{};
    in.read(reinterpret_cast<char*>(trailer.data()), trailer.size());
    if (static_cast<std::size_t>(in.gcount()) != trailer.size() ||
        crc32(section) != (uint32_t{trailer[8]} | (uint32_t{trailer[9]} << 8U) |
                           (uint32_t{trailer[10]} << 16U) | (uint32_t{trailer[11]} << 24U)))
    {
        return corrupt();
    }

    Reader reader(section);
    uint64_t version = 0;
    uint64_t chunk_bytes = 0;
    uint64_t overlap = 0;
    uint64_t rate = 0;
    uint64_t count = 0;
    if (!reader.get(1, version) || version != section_version || !reader.get(4, chunk_bytes) ||
        chunk_bytes != fm_chunk_bytes || !reader.get(4, overlap) || overlap != fm_overlap ||
        !reader.get(4, rate) || rate != fm_sample_rate || !reader.get(4, count) ||
        count != full_chunks(total_size) + 1)
    {
        return corrupt();
    }
    FmIndex index{.total_size = total_size, .chunks = {}};
    for (uint64_t i = 0; i < count; ++i)
    {
        uint64_t size = 0;
        std::span<const uint8_t> chunk;
        if (!reader.get(8, size) || !reader.take(size, chunk))
        {
            return corrupt();
        }
        index.chunks.emplace_back(chunk.begin(), chunk.end());
    }
    if (!reader.done())
    {
        return corrupt();
    }
    return index;
}

std::expected<std::vector<uint64_t>, Error> fm_find(const FmIndex& index, std::string_view pattern,
                                                    unsigned threads)
{
    if (pattern.empty() || pattern.size() > fm_overlap)
    {
        return std::unexpected(Error::corrupt_index);
    }
    using Found = std::expected<std::vector<uint64_t>, Error>;
    const auto search_chunk = [&index, pattern](std::size_t i) -> Found {
        const uint64_t start = uint64_t{i} * fm_chunk_bytes;
        const bool last = i + 1 == index.chunks.size();
        const uint64_t expected =
            last ? index.total_size - start : uint64_t{fm_chunk_bytes} + fm_overlap;
        Chunk chunk;
        uint32_t length = 0;
        if (auto loaded = chunk.load(index.chunks[i], length); !loaded)
        {
            return std::unexpected(loaded.error());
        }
        if (length != expected)
        {
            return corrupt();
        }
        auto positions = chunk.find(pattern);
        if (!positions)
        {
            return std::unexpected(positions.error());
        }
        std::vector<uint64_t> found;
        for (const uint32_t position : *positions)
        {
            // Each match once: in the chunk where it starts before the overlap, or in the last.
            if (last || position < fm_chunk_bytes)
            {
                found.push_back(start + position);
            }
        }
        return found;
    };

    ThreadPool pool(threads == 0 ? default_thread_count() : threads);
    std::vector<std::future<Found>> pending;
    pending.reserve(index.chunks.size());
    for (std::size_t i = 0; i < index.chunks.size(); ++i)
    {
        pending.push_back(pool.submit([&search_chunk, i](std::size_t /*worker*/) {
            return search_chunk(i);
        }));
    }
    std::vector<uint64_t> found;
    std::optional<Error> failure;
    for (std::future<Found>& chunk : pending)
    {
        Found result = chunk.get();
        if (!result)
        {
            failure = failure.value_or(result.error());
            continue;
        }
        found.insert(found.end(), result->begin(), result->end());
    }
    if (failure)
    {
        return std::unexpected(*failure);
    }
    return found;
}

} // namespace compressor
