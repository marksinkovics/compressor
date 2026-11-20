#include <compressor/transform/transform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

} // namespace

std::optional<Transform> parse_transform(uint8_t id, uint32_t parameter)
{
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
    }
}

std::vector<Transform> choose_transforms(std::span<const uint8_t> bytes, bool thorough)
{
    const std::span<const uint8_t> sample = bytes.first(std::min(bytes.size(), sample_bytes));
    if (sample.size() < 1024)
    {
        return {};
    }
    const std::size_t looser = thorough ? 2 : 1;
    std::vector<Transform> chosen;

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
        chosen.push_back({.id = TransformId::arm64_branch, .parameter = 0});
    }
    if (e8 * 150 * looser > sample.size())
    {
        chosen.push_back({.id = TransformId::x86_branch, .parameter = 0});
    }

    std::array<uint32_t, 256> counts{};
    for (const uint8_t byte : sample)
    {
        ++counts[byte];
    }
    const double raw = entropy(counts, sample.size());
    const double limit = raw * (thorough ? 0.95 : worthwhile_gain);
    std::vector<std::pair<double, uint32_t>> strides;
    for (const uint32_t stride : candidate_strides)
    {
        const double bits = delta_entropy(sample, stride);
        if (bits < limit)
        {
            strides.emplace_back(bits, stride);
        }
    }
    std::ranges::sort(strides);
    for (const auto& [bits, stride] : strides)
    {
        chosen.push_back({.id = TransformId::delta, .parameter = stride});
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
