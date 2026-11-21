#include <compressor/dictionary/dictionary.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

#include <compressor/checksum/crc32.h>
#include <compressor/deflate/deflate.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>
#include <compressor/lz77/lz77.h>

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

constexpr std::array<uint8_t, 4> magic{0x89, 'I', 'N', 'D'};
constexpr uint8_t file_version = 1;

// A complete prefix code (Kraft sum exactly 1) with lengths up to 15.
bool complete_code(std::span<const uint8_t> lengths)
{
    uint64_t sum = 0;
    for (const uint8_t length : lengths)
    {
        if (length > max_code_length)
        {
            return false;
        }
        if (length > 0)
        {
            sum += uint64_t{1} << (max_code_length - length);
        }
    }
    return sum == uint64_t{1} << max_code_length;
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
    return {.bytes = std::move(bytes), .id = id, .codes = std::nullopt};
}

std::vector<uint8_t> write_dictionary_file(std::span<const uint8_t> content,
                                           const DeflateCodeLengths& codes)
{
    std::vector<uint8_t> file(magic.begin(), magic.end());
    file.push_back(file_version);
    const auto nibbles = [&](std::span<const uint8_t> lengths, std::size_t count) {
        for (std::size_t i = 0; i < count; i += 2)
        {
            const unsigned low = i < lengths.size() ? lengths[i] : 0U;
            const unsigned high = i + 1 < lengths.size() && i + 1 < count ? lengths[i + 1] : 0U;
            file.push_back(static_cast<uint8_t>(low | (high << 4U)));
        }
    };
    nibbles(codes.literals, deflate_literal_symbols);
    nibbles(codes.distances, deflate_distance_symbols);
    file.insert(file.end(), content.begin(), content.end());
    return file;
}

std::expected<Dictionary, Error> parse_dictionary(std::vector<uint8_t> file)
{
    if (file.empty())
    {
        return std::unexpected(Error::corrupt_header);
    }
    if (file.size() > dictionary_header_bytes &&
        std::equal(magic.begin(), magic.end(), file.begin()) && file[4] == file_version)
    {
        const auto unpack = [&](std::size_t at, std::size_t count) {
            std::vector<uint8_t> lengths(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const uint8_t byte = file[at + (i / 2)];
                lengths[i] = static_cast<uint8_t>(i % 2 == 0 ? byte & 0xFU : byte >> 4U);
            }
            return lengths;
        };
        DeflateCodeLengths codes{.literals = unpack(5, deflate_literal_symbols),
                                 .distances = unpack(5 + 143, deflate_distance_symbols)};
        if (complete_code(codes.literals) && complete_code(codes.distances) &&
            codes.literals[256] != 0 &&
            file.size() - dictionary_header_bytes <= max_dictionary_bytes)
        {
            const uint32_t id = crc32(file);
            return Dictionary{
                .bytes = std::vector<uint8_t>(file.begin() + dictionary_header_bytes, file.end()),
                .id = id,
                .codes = std::move(codes)};
        }
    }
    if (file.size() > max_dictionary_bytes)
    {
        return std::unexpected(Error::corrupt_header);
    }
    return make_dictionary(std::move(file));
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
    return parse_dictionary(std::move(bytes));
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

DeflateCodeLengths train_codes(std::span<const std::vector<uint8_t>> samples,
                               std::span<const uint8_t> content)
{
    std::vector<uint64_t> literals(deflate_literal_symbols, 1);
    std::vector<uint64_t> distances(deflate_distance_symbols, 1);
    const Lz77Options options = block_options(6);
    for (const std::vector<uint8_t>& sample : samples)
    {
        count_deflate_symbols(sample, content, options, literals, distances);
    }
    return {.literals = code_lengths(literals, max_code_length),
            .distances = code_lengths(distances, max_code_length)};
}

std::vector<uint8_t> train_dictionary_file(std::span<const std::vector<uint8_t>> samples,
                                           std::size_t size)
{
    const std::vector<uint8_t> content = train_dictionary(samples, size);
    if (content.empty())
    {
        return {};
    }
    return write_dictionary_file(content, train_codes(samples, content));
}

} // namespace compressor
