#include <compressor/fmindex/fmindex.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <future>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <compressor/bwt/bwt.h>
#include <compressor/checksum/crc32.h>
#include <compressor/error.h>
#include <compressor/thread/pool.h>

namespace compressor
{

namespace
{

static_assert(std::endian::native == std::endian::little, "the index is read in place");

constexpr uint8_t section_version = 2;
constexpr unsigned levels = 8;
constexpr std::size_t words_per_super = 8; // a rank directory entry per 512 bits
constexpr std::size_t section_header_bytes = 1 + 4 + 4 + 4 + 4;
constexpr std::size_t table_entry_bytes = 16;

std::unexpected<Error> corrupt()
{
    return std::unexpected(Error::corrupt_index);
}

void put_le(std::vector<uint8_t>& out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
    {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

template <class T> T load(const uint8_t* at)
{
    T value{};
    std::memcpy(&value, at, sizeof(T));
    return value;
}

std::size_t word_count(std::size_t bits)
{
    return (bits + 63) / 64;
}

std::size_t super_count(std::size_t words)
{
    return (words / words_per_super) + 1;
}

// A bit vector and its rank directory: the ones before every 8th word.
void put_bits(std::vector<uint8_t>& out, const std::vector<uint64_t>& words)
{
    uint32_t ones = 0;
    std::vector<uint32_t> supers;
    for (std::size_t w = 0; w < words.size(); ++w)
    {
        if (w % words_per_super == 0)
        {
            supers.push_back(ones);
        }
        ones += static_cast<uint32_t>(std::popcount(words[w]));
    }
    if (words.size() % words_per_super == 0)
    {
        supers.push_back(ones);
    }
    for (const uint64_t word : words)
    {
        put_le(out, word, 8);
    }
    for (const uint32_t super : supers)
    {
        put_le(out, super, 4);
    }
}

std::size_t chunk_bytes_for(std::size_t n, std::size_t samples)
{
    const std::size_t level_words = word_count(n);
    const std::size_t sample_words = word_count(n + 1);
    return 8 + (256 * 4) + (levels * 4) +
           (levels * ((level_words * 8) + (super_count(level_words) * 4))) + (sample_words * 8) +
           ((sample_words + 1) * 4) + 4 + (samples * 4);
}

std::vector<uint8_t> build_chunk(std::span<const uint8_t> data)
{
    const std::size_t n = data.size();
    const std::vector<uint32_t> sa = suffix_array(data);
    // The BWT without the end marker's row, as the BWT coder keeps it.
    std::vector<uint8_t> sequence;
    sequence.reserve(n);
    uint32_t primary = 0;
    if (n > 0)
    {
        sequence.push_back(data[n - 1]);
    }
    std::vector<uint64_t> sampled(word_count(n + 1));
    std::vector<uint32_t> samples;
    const auto mark = [&](std::size_t row, uint32_t position) {
        if (position % fm_sample_rate == 0)
        {
            sampled[row / 64] |= uint64_t{1} << (row % 64);
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
        sequence.push_back(data[position - 1]);
    }

    std::array<uint32_t, 256> first{};
    {
        std::array<uint32_t, 256> counts{};
        for (const uint8_t byte : sequence)
        {
            ++counts[byte];
        }
        uint32_t next = 1; // row 0 is the end marker's
        for (std::size_t c = 0; c < 256; ++c)
        {
            first[c] = next;
            next += counts[c];
        }
    }

    std::vector<uint8_t> chunk;
    chunk.reserve(chunk_bytes_for(n, samples.size()));
    put_le(chunk, n, 4);
    put_le(chunk, primary, 4);
    for (const uint32_t value : first)
    {
        put_le(chunk, value, 4);
    }
    // Wavelet matrix: level l holds bit 7-l of every symbol, in the order the levels above
    // left them (zeros first, then ones, each in their previous order).
    std::vector<std::vector<uint64_t>> planes(levels, std::vector<uint64_t>(word_count(n)));
    std::array<uint32_t, levels> zeros{};
    std::vector<uint8_t> next(n);
    for (unsigned level = 0; level < levels; ++level)
    {
        const unsigned shift = 7 - level;
        std::size_t zero_at = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (((unsigned{sequence[i]} >> shift) & 1U) != 0)
            {
                planes[level][i / 64] |= uint64_t{1} << (i % 64);
            }
            else
            {
                ++zero_at;
            }
        }
        zeros[level] = static_cast<uint32_t>(zero_at);
        std::size_t one_at = zero_at;
        zero_at = 0;
        for (const uint8_t symbol : sequence)
        {
            next[((unsigned{symbol} >> shift) & 1U) != 0 ? one_at++ : zero_at++] = symbol;
        }
        sequence.swap(next);
    }
    for (const uint32_t value : zeros)
    {
        put_le(chunk, value, 4);
    }
    for (const std::vector<uint64_t>& plane : planes)
    {
        put_bits(chunk, plane);
    }
    // Sampled rows, and the ones before every word (a directory per word, so a lookup is one
    // popcount).
    uint32_t ones = 0;
    std::vector<uint32_t> word_ranks;
    for (const uint64_t word : sampled)
    {
        word_ranks.push_back(ones);
        ones += static_cast<uint32_t>(std::popcount(word));
    }
    word_ranks.push_back(ones);
    for (const uint64_t word : sampled)
    {
        put_le(chunk, word, 8);
    }
    for (const uint32_t rank : word_ranks)
    {
        put_le(chunk, rank, 4);
    }
    put_le(chunk, samples.size(), 4);
    for (const uint32_t sample : samples)
    {
        put_le(chunk, sample, 4);
    }
    return chunk;
}

// One chunk read in place. Every value from the file is checked before it indexes anything;
// `broken` records that a value didn't add up.
class Chunk
{
public:
    std::expected<void, Error> open(std::span<const uint8_t> bytes, std::size_t expected_n)
    {
        if (bytes.size() < 8 + (256 * 4) + (levels * 4))
        {
            return corrupt();
        }
        n_ = load<uint32_t>(bytes.data());
        primary_ = load<uint32_t>(bytes.data() + 4);
        if (n_ != expected_n || (n_ > 0 && (primary_ == 0 || primary_ > n_)))
        {
            return corrupt();
        }
        const uint8_t* at = bytes.data() + 8;
        uint32_t previous = 1;
        for (uint32_t& value : first_)
        {
            value = load<uint32_t>(at);
            at += 4;
            if (value < previous || value > n_ + 1)
            {
                return corrupt();
            }
            previous = value;
        }
        for (uint32_t& value : zeros_)
        {
            value = load<uint32_t>(at);
            at += 4;
            if (value > n_)
            {
                return corrupt();
            }
        }
        level_words_ = word_count(n_);
        for (unsigned level = 0; level < levels; ++level)
        {
            planes_[level] = at;
            at += level_words_ * 8;
            supers_[level] = at;
            at += super_count(level_words_) * 4;
        }
        sample_words_ = word_count(n_ + 1);
        sampled_ = at;
        at += sample_words_ * 8;
        word_ranks_ = at;
        at += (sample_words_ + 1) * 4;
        const auto header = static_cast<std::size_t>(at - bytes.data());
        if (bytes.size() < header + 4)
        {
            return corrupt();
        }
        sample_count_ = load<uint32_t>(at);
        samples_ = at + 4;
        if (bytes.size() != chunk_bytes_for(n_, sample_count_))
        {
            return corrupt();
        }
        return {};
    }

    // The rows of the suffixes that start with `pattern`.
    std::expected<std::pair<std::size_t, std::size_t>, Error> rows(std::string_view pattern)
    {
        std::size_t begin = 0;
        std::size_t end = n_ + 1;
        for (std::size_t k = pattern.size(); k-- > 0 && begin < end;)
        {
            const auto c = static_cast<uint8_t>(pattern[k]);
            begin = first_[c] + rank(c, begin);
            end = first_[c] + rank(c, end);
            if (broken_ || end > n_ + 1)
            {
                return corrupt();
            }
        }
        return std::pair{begin, std::max(begin, end)};
    }

    std::expected<std::vector<uint32_t>, Error> find(std::string_view pattern)
    {
        const auto found = rows(pattern);
        if (!found)
        {
            return std::unexpected(found.error());
        }
        const auto [begin, end] = *found;
        std::vector<uint32_t> positions;
        for (std::size_t row = begin; row < end; ++row)
        {
            const std::optional<uint32_t> position = locate(row);
            if (!position || broken_)
            {
                return corrupt();
            }
            positions.push_back(*position);
        }
        std::ranges::sort(positions);
        return positions;
    }

private:
    // Ones in plane `level` before bit `i` (i <= n).
    std::size_t rank1(unsigned level, std::size_t i)
    {
        const std::size_t word = i / 64;
        const std::size_t super = word / words_per_super;
        std::size_t ones = load<uint32_t>(supers_[level] + (super * 4));
        for (std::size_t w = super * words_per_super; w < word; ++w)
        {
            ones +=
                static_cast<std::size_t>(std::popcount(load<uint64_t>(planes_[level] + (w * 8))));
        }
        if (i % 64 != 0)
        {
            const auto bits = load<uint64_t>(planes_[level] + (word * 8));
            ones += static_cast<std::size_t>(std::popcount(bits & ((uint64_t{1} << (i % 64)) - 1)));
        }
        if (ones > i)
        {
            broken_ = true;
            return i;
        }
        return ones;
    }

    // Where position `i` of a level goes in the next.
    std::size_t next(unsigned level, std::size_t i, unsigned bit)
    {
        const std::size_t ones = rank1(level, i);
        const std::size_t moved = bit != 0 ? zeros_[level] + ones : i - ones;
        if (moved > n_)
        {
            broken_ = true;
            return 0;
        }
        return moved;
    }

    // Occurrences of `c` in the stored sequence before index `t` (t <= n).
    std::size_t count(uint8_t c, std::size_t t)
    {
        std::size_t start = 0;
        std::size_t stop = t;
        for (unsigned level = 0; level < levels; ++level)
        {
            const unsigned bit = (unsigned{c} >> (7 - level)) & 1U;
            start = next(level, start, bit);
            stop = next(level, stop, bit);
        }
        return stop >= start ? stop - start : 0;
    }

    uint8_t symbol_at(std::size_t t)
    {
        unsigned c = 0;
        for (unsigned level = 0; level < levels && !broken_; ++level)
        {
            const unsigned bit = (load<uint64_t>(planes_[level] + ((t / 64) * 8)) >> (t % 64)) & 1U;
            c = (c << 1U) | bit;
            t = next(level, t, bit);
            if (t >= n_ && level + 1 < levels)
            {
                broken_ = true;
            }
        }
        return static_cast<uint8_t>(c);
    }

    // Occurrences of `c` in the last column above `row` (the marker's row holds nothing).
    std::size_t rank(uint8_t c, std::size_t row)
    {
        return count(c, row <= primary_ ? row : row - 1);
    }

    std::optional<uint32_t> sample(std::size_t row)
    {
        const auto word = load<uint64_t>(sampled_ + ((row / 64) * 8));
        const uint64_t bit = uint64_t{1} << (row % 64);
        if ((word & bit) == 0)
        {
            return std::nullopt;
        }
        const std::size_t index = load<uint32_t>(word_ranks_ + ((row / 64) * 4)) +
                                  static_cast<std::size_t>(std::popcount(word & (bit - 1)));
        if (index >= sample_count_)
        {
            broken_ = true;
            return std::nullopt;
        }
        return load<uint32_t>(samples_ + (index * 4));
    }

    // Walks LF back to a sampled row; each step is one position earlier in the text.
    std::optional<uint32_t> locate(std::size_t row)
    {
        for (uint32_t steps = 0; steps <= fm_sample_rate && !broken_; ++steps)
        {
            if (const auto value = sample(row))
            {
                return *value + steps;
            }
            if (row == primary_)
            {
                return steps; // the suffix starting at 0
            }
            const std::size_t t = row < primary_ ? row : row - 1;
            if (t >= n_)
            {
                return std::nullopt;
            }
            const uint8_t c = symbol_at(t);
            row = first_[c] + count(c, t);
            if (row > n_)
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::size_t n_ = 0;
    std::size_t primary_ = 0;
    std::array<uint32_t, 256> first_{};
    std::array<uint32_t, levels> zeros_{};
    std::size_t level_words_ = 0;
    std::array<const uint8_t*, levels> planes_{};
    std::array<const uint8_t*, levels> supers_{};
    std::size_t sample_words_ = 0;
    const uint8_t* sampled_ = nullptr;
    const uint8_t* word_ranks_ = nullptr;
    std::size_t sample_count_ = 0;
    const uint8_t* samples_ = nullptr;
    bool broken_ = false;
};

// The builder cuts a full chunk (chunk + overlap bytes) whenever that much is pending, then keeps
// the overlap; whatever is left at the end, at least the overlap, is the last chunk.
uint64_t full_chunks(uint64_t total)
{
    return total >= uint64_t{fm_chunk_bytes} + fm_overlap ? (total - fm_overlap) / fm_chunk_bytes
                                                          : 0;
}

} // namespace

FmIndexBuilder::FmIndexBuilder(unsigned threads)
    : pool_(threads == 0 ? default_thread_count() : threads)
    , max_in_flight_(2 * pool_.size())
    , spool_(std::tmpfile())
{
    failed_ = spool_ == nullptr;
}

FmIndexBuilder::~FmIndexBuilder()
{
    while (!in_flight_.empty())
    {
        in_flight_.front().wait();
        in_flight_.pop_front();
    }
    if (spool_ != nullptr)
    {
        std::fclose(spool_);
    }
}

void FmIndexBuilder::submit(std::vector<uint8_t> data)
{
    if (in_flight_.size() >= max_in_flight_)
    {
        spool_oldest();
    }
    in_flight_.push_back(pool_.submit([data = std::move(data)](std::size_t /*worker*/) {
        return build_chunk(data);
    }));
}

bool FmIndexBuilder::spool_oldest()
{
    const std::vector<uint8_t> chunk = in_flight_.front().get();
    in_flight_.pop_front();
    chunk_sizes_.push_back(chunk.size());
    if (!failed_ && std::fwrite(chunk.data(), 1, chunk.size(), spool_) != chunk.size())
    {
        failed_ = true;
    }
    return !failed_;
}

void FmIndexBuilder::add(std::span<const uint8_t> bytes)
{
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    const std::size_t full = std::size_t{fm_chunk_bytes} + fm_overlap;
    while (pending_.size() >= full)
    {
        submit({pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(full)});
        pending_.erase(pending_.begin(), pending_.begin() + fm_chunk_bytes);
    }
}

bool FmIndexBuilder::write(std::ostream& out, uint64_t section_offset)
{
    if (!pending_.empty() || (chunk_sizes_.empty() && in_flight_.empty()))
    {
        submit(std::move(pending_));
        pending_.clear();
    }
    while (!in_flight_.empty())
    {
        spool_oldest();
    }
    if (failed_ || std::fflush(spool_) != 0 || std::fseek(spool_, 0, SEEK_SET) != 0)
    {
        return false;
    }

    std::vector<uint8_t> head{section_version};
    put_le(head, fm_chunk_bytes, 4);
    put_le(head, fm_overlap, 4);
    put_le(head, fm_sample_rate, 4);
    put_le(head, chunk_sizes_.size(), 4);
    uint64_t offset = section_header_bytes + (chunk_sizes_.size() * table_entry_bytes);
    for (const uint64_t size : chunk_sizes_)
    {
        put_le(head, offset, 8);
        put_le(head, size, 8);
        offset += size;
    }
    out.write(reinterpret_cast<const char*>(head.data()),
              static_cast<std::streamsize>(head.size()));
    uint32_t crc = crc32(head);
    std::vector<uint8_t> buffer(std::size_t{1} << 20U);
    while (true)
    {
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), spool_);
        if (got == 0)
        {
            break;
        }
        crc = crc32(std::span(buffer).first(got), crc);
        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(got));
    }
    if (std::ferror(spool_) != 0)
    {
        return false;
    }
    std::vector<uint8_t> trailer;
    put_le(trailer, section_offset, 8);
    put_le(trailer, crc, 4);
    trailer.insert(trailer.end(), search_index_magic.begin(), search_index_magic.end());
    out.write(reinterpret_cast<const char*>(trailer.data()),
              static_cast<std::streamsize>(trailer.size()));
    return static_cast<bool>(out);
}

std::expected<FmIndex, Error> parse_search_index(std::span<const uint8_t> section,
                                                 uint64_t total_size,
                                                 std::shared_ptr<const void> owner)
{
    if (section.size() < section_header_bytes || section[0] != section_version ||
        load<uint32_t>(section.data() + 1) != fm_chunk_bytes ||
        load<uint32_t>(section.data() + 5) != fm_overlap ||
        load<uint32_t>(section.data() + 9) != fm_sample_rate)
    {
        return corrupt();
    }
    const uint64_t count = load<uint32_t>(section.data() + 13);
    if (count != full_chunks(total_size) + 1 ||
        count > (section.size() - section_header_bytes) / table_entry_bytes)
    {
        return corrupt();
    }
    FmIndex index{.owner = std::move(owner), .total_size = total_size, .chunks = {}};
    for (uint64_t i = 0; i < count; ++i)
    {
        const uint8_t* entry = section.data() + section_header_bytes + (i * table_entry_bytes);
        const auto offset = load<uint64_t>(entry);
        const auto size = load<uint64_t>(entry + 8);
        if (offset > section.size() || size > section.size() - offset)
        {
            return corrupt();
        }
        index.chunks.push_back(section.subspan(offset, size));
    }
    return index;
}

std::expected<FmIndex, Error> map_search_index(const std::filesystem::path& archive,
                                               uint64_t section_offset, uint64_t section_end,
                                               uint64_t total_size)
{
    if (section_end < section_offset)
    {
        return corrupt();
    }
#if defined(_WIN32)
    // No mapping here: read the section.
    std::ifstream file(archive, std::ios::in | std::ios::binary);
    auto bytes = std::make_shared<std::vector<uint8_t>>(section_end - section_offset);
    file.seekg(static_cast<std::streamoff>(section_offset));
    file.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (static_cast<std::size_t>(file.gcount()) != bytes->size())
    {
        return std::unexpected(Error::read_failed);
    }
    return parse_search_index(*bytes, total_size, bytes);
#else
    const int fd = ::open(archive.c_str(), O_RDONLY);
    if (fd < 0)
    {
        return std::unexpected(Error::read_failed);
    }
    struct stat status{};
    if (::fstat(fd, &status) != 0 || std::cmp_less(status.st_size, section_end))
    {
        ::close(fd);
        return std::unexpected(Error::truncated);
    }
    const auto length = static_cast<std::size_t>(status.st_size);
    void* mapped = length == 0 ? nullptr : ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED || mapped == nullptr)
    {
        return std::unexpected(Error::read_failed);
    }
    std::shared_ptr<const void> owner(mapped, [length](const void* pointer) {
        ::munmap(const_cast<void*>(pointer), length);
    });
    const std::span<const uint8_t> file(static_cast<const uint8_t*>(mapped), length);
    return parse_search_index(file.subspan(section_offset, section_end - section_offset),
                              total_size, std::move(owner));
#endif
}

std::expected<void, Error> check_search_index(std::istream& in, uint64_t section_offset,
                                              uint64_t section_end)
{
    if (section_end < section_offset)
    {
        return corrupt();
    }
    in.clear();
    in.seekg(static_cast<std::streamoff>(section_offset));
    std::vector<char> buffer(std::size_t{1} << 20U);
    uint32_t crc = 0;
    for (uint64_t left = section_end - section_offset; left > 0;)
    {
        const auto take = static_cast<std::streamsize>(std::min<uint64_t>(left, buffer.size()));
        in.read(buffer.data(), take);
        if (in.gcount() != take)
        {
            return std::unexpected(in.bad() ? Error::read_failed : Error::truncated);
        }
        crc = crc32(
            {reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<std::size_t>(take)}, crc);
        left -= static_cast<uint64_t>(take);
    }
    std::array<uint8_t, search_trailer_bytes> trailer{};
    in.read(reinterpret_cast<char*>(trailer.data()), trailer.size());
    if (static_cast<std::size_t>(in.gcount()) != trailer.size() ||
        crc != load<uint32_t>(trailer.data() + 8))
    {
        return corrupt();
    }
    return {};
}

std::expected<uint64_t, Error> fm_count(const FmIndex& index, std::string_view pattern,
                                        unsigned threads)
{
    if (pattern.empty() || pattern.size() > fm_overlap)
    {
        return std::unexpected(Error::corrupt_index);
    }
    using Counted = std::expected<uint64_t, Error>;
    ThreadPool pool(threads == 0 ? default_thread_count() : threads);
    std::vector<std::future<Counted>> pending;
    pending.reserve(index.chunks.size());
    for (std::size_t i = 0; i < index.chunks.size(); ++i)
    {
        pending.push_back(pool.submit([&index, pattern, i](std::size_t /*worker*/) -> Counted {
            const uint64_t start = uint64_t{i} * fm_chunk_bytes;
            const bool last = i + 1 == index.chunks.size();
            Chunk chunk;
            if (auto opened = chunk.open(
                    index.chunks[i],
                    static_cast<std::size_t>(last ? index.total_size - start
                                                  : uint64_t{fm_chunk_bytes} + fm_overlap));
                !opened)
            {
                return std::unexpected(opened.error());
            }
            const auto rows = chunk.rows(pattern);
            if (!rows)
            {
                return std::unexpected(rows.error());
            }
            return rows->second - rows->first;
        }));
    }
    uint64_t total = 0;
    std::optional<Error> failure;
    for (std::future<Counted>& chunk : pending)
    {
        const Counted result = chunk.get();
        if (!result)
        {
            failure = failure.value_or(result.error());
            continue;
        }
        total += *result;
    }
    if (failure)
    {
        return std::unexpected(*failure);
    }
    return total;
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
        if (auto opened = chunk.open(index.chunks[i], static_cast<std::size_t>(expected)); !opened)
        {
            return std::unexpected(opened.error());
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
