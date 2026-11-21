#include <compressor/chunk/chunk.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace compressor
{

namespace
{

constexpr uint64_t mix(uint64_t x)
{
    x ^= x >> 33U;
    x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33U;
    x *= 0xC4CEB9FE1A85EC53ULL;
    x ^= x >> 33U;
    return x;
}

constexpr std::array<uint64_t, 256> gear = [] {
    std::array<uint64_t, 256> table{};
    uint64_t state = 1;
    for (uint64_t& entry : table)
    {
        state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
        entry = mix(state);
    }
    return table;
}();

uint64_t load64(const uint8_t* bytes, std::size_t count)
{
    uint64_t value = 0;
    std::memcpy(&value, bytes, count);
    return value;
}

} // namespace

std::vector<std::size_t> chunk_ends(std::span<const uint8_t> bytes, std::size_t average)
{
    const auto bits = static_cast<unsigned>(std::countr_zero(average));
    // The hash's top bits mix the most bytes. A mask with one bit more than the average's before
    // the average size and one bit fewer after it keeps chunk sizes close to the average.
    const uint64_t strict = ((uint64_t{1} << (bits + 1)) - 1) << (63 - bits);
    const uint64_t loose = ((uint64_t{1} << (bits - 1)) - 1) << (65 - bits);
    const std::size_t min_size = average / 4;
    const std::size_t max_size = average * 8;

    std::vector<std::size_t> ends;
    ends.reserve((bytes.size() / average) + 1);
    std::size_t at = 0;
    while (at < bytes.size())
    {
        const std::size_t end = std::min(bytes.size(), at + max_size);
        const std::size_t middle = std::min(end, at + average);
        std::size_t i = std::min(end, at + min_size);
        uint64_t hash = 0;
        bool cut = false;
        for (; i < middle && !cut; ++i)
        {
            hash = (hash << 1U) + gear[bytes[i]];
            cut = (hash & strict) == 0;
        }
        for (; i < end && !cut; ++i)
        {
            hash = (hash << 1U) + gear[bytes[i]];
            cut = (hash & loose) == 0;
        }
        at = i;
        ends.push_back(at);
    }
    return ends;
}

uint64_t chunk_hash(std::span<const uint8_t> bytes)
{
    // Four independent lanes, so the multiplies overlap.
    std::array<uint64_t, 4> lanes{0x9E3779B97F4A7C15ULL, 0xC2B2AE3D27D4EB4FULL,
                                  0x165667B19E3779F9ULL, 0x27D4EB2F165667C5ULL};
    std::size_t i = 0;
    for (; i + 32 <= bytes.size(); i += 32)
    {
        for (std::size_t lane = 0; lane < 4; ++lane)
        {
            lanes[lane] = mix(lanes[lane] ^ load64(bytes.data() + i + (8 * lane), 8));
        }
    }
    uint64_t hash = uint64_t{bytes.size()} ^ lanes[0] ^ std::rotl(lanes[1], 16) ^
                    std::rotl(lanes[2], 32) ^ std::rotl(lanes[3], 48);
    for (; i + 8 <= bytes.size(); i += 8)
    {
        hash = mix(hash ^ load64(bytes.data() + i, 8));
    }
    return mix(hash ^ load64(bytes.data() + i, bytes.size() - i) ^ (uint64_t{bytes.size()} << 8U));
}

} // namespace compressor
