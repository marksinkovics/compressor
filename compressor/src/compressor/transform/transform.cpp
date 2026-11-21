#include <compressor/transform/transform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace compressor
{

namespace
{

constexpr std::size_t sample_bytes = std::size_t{64} * 1024;
constexpr std::array<uint32_t, 7> candidate_strides{1, 2, 3, 4, 6, 8, 16};
constexpr double worthwhile_gain = 0.9; // the transform's entropy must be below 90% of the raw one

uint32_t load_le32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

void store_le32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
    bytes[2] = static_cast<uint8_t>(value >> 16U);
    bytes[3] = static_cast<uint8_t>(value >> 24U);
}

constexpr uint32_t bl_mask = 0xFC000000;
constexpr uint32_t bl_opcode = 0x94000000;
constexpr uint32_t imm26_mask = 0x03FFFFFF;

// BL's 26-bit word offset made absolute (or relative again) against the word's position. The
// opcode bits are left alone, so decoding finds exactly the words encoding changed.
void arm64_branches(std::span<uint8_t> bytes, bool encode)
{
    for (std::size_t at = 0; at + 4 <= bytes.size(); at += 4)
    {
        const uint32_t word = load_le32(bytes.data() + at);
        if ((word & bl_mask) != bl_opcode)
        {
            continue;
        }
        const auto position = static_cast<uint32_t>(at / 4);
        const uint32_t offset = word & imm26_mask;
        const uint32_t changed = encode ? offset + position : offset - position;
        store_le32(bytes.data() + at, bl_opcode | (changed & imm26_mask));
    }
}

// Every E8/E9 byte's 32-bit operand made absolute (or relative) against the end of the
// instruction. The scan skips the operand it changed, and never looks at the bytes it changes,
// so encoding and decoding stop at the same places.
void x86_branches(std::span<uint8_t> bytes, bool encode)
{
    for (std::size_t at = 0; at + 5 <= bytes.size();)
    {
        if (bytes[at] != 0xE8 && bytes[at] != 0xE9)
        {
            ++at;
            continue;
        }
        const auto next = static_cast<uint32_t>(at + 5);
        const uint32_t operand = load_le32(bytes.data() + at + 1);
        store_le32(bytes.data() + at + 1, encode ? operand + next : operand - next);
        at += 5;
    }
}

uint32_t row_bytes_of(uint32_t parameter)
{
    return parameter & 0xFFFFFFU;
}

uint32_t pixel_bytes_of(uint32_t parameter)
{
    return parameter >> 24U;
}

uint8_t med(uint8_t left, uint8_t up, uint8_t up_left)
{
    const auto [low, high] = std::minmax(left, up);
    if (up_left >= high)
    {
        return low;
    }
    if (up_left <= low)
    {
        return high;
    }
    return static_cast<uint8_t>(left + up - up_left);
}

// The prediction for bytes[at] from the bytes before it; missing neighbours are 0.
uint8_t predict(std::span<const uint8_t> bytes, std::size_t at, std::size_t row, std::size_t pixel)
{
    const uint8_t left = at >= pixel ? bytes[at - pixel] : 0;
    const uint8_t up = at >= row ? bytes[at - row] : 0;
    const uint8_t up_left = at >= row + pixel ? bytes[at - row - pixel] : 0;
    return med(left, up, up_left);
}

// Back to front, so every prediction still sees the original bytes.
void apply_image(uint32_t parameter, std::span<uint8_t> bytes)
{
    const std::size_t row = row_bytes_of(parameter);
    const std::size_t pixel = pixel_bytes_of(parameter);
    for (std::size_t i = bytes.size(); i-- > 0;)
    {
        bytes[i] = static_cast<uint8_t>(bytes[i] - predict(bytes, i, row, pixel));
    }
}

void undo_image(uint32_t parameter, std::span<uint8_t> bytes)
{
    const std::size_t row = row_bytes_of(parameter);
    const std::size_t pixel = pixel_bytes_of(parameter);
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] = static_cast<uint8_t>(bytes[i] + predict(bytes, i, row, pixel));
    }
}

double entropy(const std::array<uint32_t, 256>& counts, std::size_t total)
{
    double bits = 0;
    for (const uint32_t count : counts)
    {
        if (count != 0)
        {
            const double p = static_cast<double>(count) / static_cast<double>(total);
            bits -= p * std::log2(p);
        }
    }
    return bits;
}

double delta_entropy(std::span<const uint8_t> sample, uint32_t stride)
{
    std::array<uint32_t, 256> counts{};
    for (std::size_t i = 0; i < sample.size(); ++i)
    {
        const uint8_t before = i >= stride ? sample[i - stride] : 0;
        ++counts[static_cast<uint8_t>(sample[i] - before)];
    }
    return entropy(counts, sample.size());
}

double image_entropy(std::span<const uint8_t> sample, uint32_t parameter)
{
    std::array<uint32_t, 256> counts{};
    const std::size_t row = row_bytes_of(parameter);
    const std::size_t pixel = pixel_bytes_of(parameter);
    for (std::size_t i = 0; i < sample.size(); ++i)
    {
        ++counts[static_cast<uint8_t>(sample[i] - predict(sample, i, row, pixel))];
    }
    return entropy(counts, sample.size());
}

// The distance at which bytes repeat best in the second half of the sample, from 16 bytes to a
// third of the sample: an image's row length.
std::size_t find_row_bytes(std::span<const uint8_t> sample)
{
    constexpr std::size_t min_row = 16;
    constexpr std::size_t probes = 1024;
    const std::size_t max_row = sample.size() / 3;
    const std::size_t from = sample.size() / 2;
    const std::size_t step = std::max<std::size_t>(1, (sample.size() - from) / probes);
    std::size_t best_row = 0;
    uint64_t best_cost = std::numeric_limits<uint64_t>::max();
    for (std::size_t row = min_row; row <= max_row; ++row)
    {
        uint64_t cost = 0;
        for (std::size_t at = from; at < sample.size() && cost < best_cost; at += step)
        {
            const auto difference = static_cast<uint8_t>(sample[at] - sample[at - row]);
            cost += std::min<unsigned>(difference, 256U - difference);
        }
        if (cost < best_cost)
        {
            best_cost = cost;
            best_row = row;
        }
    }
    return best_row;
}

} // namespace

uint32_t image_parameter(uint32_t row_bytes, uint32_t pixel_bytes)
{
    return (pixel_bytes << 24U) | row_bytes;
}

std::optional<Transform> parse_transform(uint8_t id, uint32_t parameter)
{
    const uint32_t pixel = pixel_bytes_of(parameter);
    if (id == static_cast<uint8_t>(TransformId::med) && pixel >= 1 && pixel <= max_pixel_bytes &&
        row_bytes_of(parameter) > pixel)
    {
        return Transform{.id = TransformId::med, .parameter = parameter};
    }
    if (id == static_cast<uint8_t>(TransformId::delta) && parameter >= 1 &&
        parameter <= max_delta_stride)
    {
        return Transform{.id = TransformId::delta, .parameter = parameter};
    }
    for (const TransformId filter : {TransformId::arm64_branch, TransformId::x86_branch})
    {
        if (id == static_cast<uint8_t>(filter) && parameter == 0)
        {
            return Transform{.id = filter, .parameter = 0};
        }
    }
    return std::nullopt;
}

void apply_transform(const Transform& transform, std::span<uint8_t> bytes)
{
    switch (transform.id)
    {
    case TransformId::delta:
        for (std::size_t i = bytes.size(); i-- > transform.parameter;)
        {
            bytes[i] = static_cast<uint8_t>(bytes[i] - bytes[i - transform.parameter]);
        }
        break;
    case TransformId::arm64_branch:
        arm64_branches(bytes, true);
        break;
    case TransformId::x86_branch:
        x86_branches(bytes, true);
        break;
    case TransformId::med:
        apply_image(transform.parameter, bytes);
        break;
    }
}

void undo_transform(const Transform& transform, std::span<uint8_t> bytes)
{
    switch (transform.id)
    {
    case TransformId::delta:
        for (std::size_t i = transform.parameter; i < bytes.size(); ++i)
        {
            bytes[i] = static_cast<uint8_t>(bytes[i] + bytes[i - transform.parameter]);
        }
        break;
    case TransformId::arm64_branch:
        arm64_branches(bytes, false);
        break;
    case TransformId::x86_branch:
        x86_branches(bytes, false);
        break;
    case TransformId::med:
        undo_image(transform.parameter, bytes);
        break;
    }
}

std::vector<Transform> choose_transforms(std::span<const uint8_t> bytes, bool thorough)
{
    // From the middle: a block's start may be a file header, unlike the rest of it.
    const std::size_t sample_size = std::min(bytes.size(), sample_bytes);
    const std::span<const uint8_t> sample =
        bytes.subspan((bytes.size() - sample_size) / 2, sample_size);
    if (sample.size() < 1024)
    {
        return {};
    }
    const std::size_t looser = thorough ? 2 : 1;
    std::vector<Transform> branches;

    // Machine code: BL is about 1 in 15 ARM64 instructions in typical code, E8 about 1 in 100
    // x86 bytes; text and media stay far below either.
    std::size_t words = 0;
    std::size_t calls = 0;
    std::size_t e8 = 0;
    for (std::size_t at = 0; at + 4 <= sample.size(); at += 4)
    {
        ++words;
        calls += static_cast<std::size_t>((load_le32(sample.data() + at) & bl_mask) == bl_opcode);
    }
    for (const uint8_t byte : sample)
    {
        e8 += static_cast<std::size_t>(byte == 0xE8);
    }
    if (calls * 40 * looser > words)
    {
        branches.push_back({.id = TransformId::arm64_branch, .parameter = 0});
    }
    if (e8 * 150 * looser > sample.size())
    {
        branches.push_back({.id = TransformId::x86_branch, .parameter = 0});
    }

    std::array<uint32_t, 256> counts{};
    for (const uint8_t byte : sample)
    {
        ++counts[byte];
    }
    const double raw = entropy(counts, sample.size());
    const double limit = raw * (thorough ? 0.95 : worthwhile_gain);
    std::vector<std::pair<double, Transform>> predictors;
    for (const uint32_t stride : candidate_strides)
    {
        const double bits = delta_entropy(sample, stride);
        if (bits < limit)
        {
            predictors.emplace_back(bits, Transform{.id = TransformId::delta, .parameter = stride});
        }
    }
    const auto by_bits = [](const auto& a, const auto& b) {
        return a.first < b.first;
    };
    std::ranges::stable_sort(predictors, by_bits);
    // Samples a pixel apart predict each other: perhaps an image, whose rows predict too.
    if (!predictors.empty() && predictors.front().second.parameter <= max_pixel_bytes)
    {
        const auto pixel = predictors.front().second.parameter;
        const std::size_t found = find_row_bytes(sample);
        // The best distance may be a diagonal neighbour of the row above.
        std::pair<double, uint32_t> best{predictors.front().first, 0};
        for (std::size_t row = found > pixel ? found - pixel : 0; found > 0 && row <= found + pixel;
             ++row)
        {
            if (row > pixel)
            {
                const uint32_t parameter = image_parameter(static_cast<uint32_t>(row), pixel);
                best = std::min(best, {image_entropy(sample, parameter), parameter});
            }
        }
        if (best.second != 0)
        {
            predictors.emplace_back(best.first,
                                    Transform{.id = TransformId::med, .parameter = best.second});
            std::ranges::stable_sort(predictors, by_bits);
        }
    }
    // Images can look like dense calls; a predictor that clearly helps says they are media.
    const bool media = !predictors.empty() && predictors.front().first < raw * worthwhile_gain;
    std::vector<Transform> chosen = media ? std::vector<Transform>{} : branches;
    for (const auto& [bits, transform] : predictors)
    {
        chosen.push_back(transform);
    }
    if (media)
    {
        chosen.insert(chosen.end(), branches.begin(), branches.end());
    }
    if (!thorough && chosen.size() > 1)
    {
        chosen.resize(1);
    }
    if (thorough && chosen.size() > 3)
    {
        chosen.resize(3);
    }
    return chosen;
}

} // namespace compressor
