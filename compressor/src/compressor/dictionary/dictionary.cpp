#include <compressor/dictionary/dictionary.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <system_error>
#include <vector>

#include <compressor/checksum/crc32.h>
#include <compressor/error.h>

namespace compressor
{

namespace
{

constexpr std::size_t dmer = 8;
constexpr std::size_t segment = 1024;
constexpr unsigned key_bits = 22;
constexpr uint32_t no_key = std::numeric_limits<uint32_t>::max();

uint32_t key(const uint8_t* bytes)
{
    uint64_t x = 0;
    std::memcpy(&x, bytes, dmer);
    x ^= x >> 31U;
    x *= 0x7FB5D329728EA185ULL;
    x ^= x >> 27U;
    x *= 0x81DADEF4BC2DD44DULL;
    x ^= x >> 33U;
    return static_cast<uint32_t>(x >> (64U - key_bits));
}

struct Segment
{
    uint64_t score = 0;
    std::size_t start = 0;
};

} // namespace

Dictionary make_dictionary(std::vector<uint8_t> bytes)
{
    const uint32_t id = crc32(bytes);
    return {.bytes = std::move(bytes), .id = id};
}

std::expected<Dictionary, Error> read_dictionary(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
    {
        return std::unexpected(Error::read_failed);
    }
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
    if (file.bad())
    {
        return std::unexpected(Error::read_failed);
    }
    if (bytes.empty() || bytes.size() > max_dictionary_bytes)
    {
        return std::unexpected(Error::corrupt_header);
    }
    return make_dictionary(std::move(bytes));
}

std::vector<uint8_t> train_dictionary(std::span<const std::vector<uint8_t>> samples,
                                      std::size_t size)
{
    size = std::min(size, max_dictionary_bytes);
    std::vector<uint8_t> all;
    for (const std::vector<uint8_t>& sample : samples)
    {
        all.insert(all.end(), sample.begin(), sample.end());
    }
    if (all.size() <= size)
    {
        return all;
    }

    // How many samples contain each string, and each position's string (none across samples).
    std::vector<uint32_t> counts(std::size_t{1} << key_bits);
    std::vector<uint32_t> last_sample(counts.size(), no_key);
    std::vector<uint32_t> keys(all.size(), no_key);
    std::size_t offset = 0;
    for (std::size_t s = 0; s < samples.size(); ++s)
    {
        const std::vector<uint8_t>& sample = samples[s];
        for (std::size_t i = 0; i + dmer <= sample.size(); ++i)
        {
            const uint32_t k = key(sample.data() + i);
            keys[offset + i] = k;
            if (last_sample[k] != s)
            {
                last_sample[k] = static_cast<uint32_t>(s);
                ++counts[k];
            }
        }
        offset += sample.size();
    }
    // A string in only one sample can't help another.
    const auto score = [&](std::size_t position) -> uint64_t {
        const uint32_t k = keys[position];
        return k != no_key && counts[k] >= 2 ? counts[k] : 0;
    };

    const std::size_t length = std::min(segment, size);
    const std::size_t wanted = std::max<std::size_t>(1, size / length);
    const std::size_t epoch = std::max(length, all.size() / wanted);
    std::vector<Segment> chosen;
    for (std::size_t begin = 0; begin + length <= all.size() && chosen.size() < wanted;
         begin += epoch)
    {
        const std::size_t end = std::min(all.size(), begin + epoch);
        // Sliding window over the segment starts in this epoch.
        uint64_t sum = 0;
        for (std::size_t i = begin; i < begin + length; ++i)
        {
            sum += score(i);
        }
        Segment best{.score = sum, .start = begin};
        for (std::size_t start = begin + 1; start + length <= end; ++start)
        {
            sum += score(start + length - 1);
            sum -= score(start - 1);
            if (sum > best.score)
            {
                best = {.score = sum, .start = start};
            }
        }
        // As if a quarter of its strings were in two samples; less is noise or hash collisions.
        if (best.score < 2 * (length / 4))
        {
            continue;
        }
        for (std::size_t i = best.start; i < best.start + length; ++i)
        {
            if (keys[i] != no_key)
            {
                counts[keys[i]] = 0;
            }
        }
        chosen.push_back(best);
    }

    std::ranges::stable_sort(chosen, {}, &Segment::score);
    std::vector<uint8_t> dictionary;
    for (const Segment& piece : chosen)
    {
        const auto first = all.begin() + static_cast<std::ptrdiff_t>(piece.start);
        dictionary.insert(dictionary.end(), first, first + static_cast<std::ptrdiff_t>(length));
    }
    if (dictionary.size() > size)
    {
        dictionary.erase(dictionary.begin(), dictionary.begin() + static_cast<std::ptrdiff_t>(
                                                                      dictionary.size() - size));
    }
    return dictionary;
}

} // namespace compressor
