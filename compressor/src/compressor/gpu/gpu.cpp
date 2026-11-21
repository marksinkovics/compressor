#include <compressor/gpu/gpu.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace compressor::gpu
{

#if !defined(COMPRESSOR_METAL)
Device* device()
{
    return nullptr;
}
#endif

void histogram(std::span<const uint8_t> bytes, std::span<uint32_t, 256> counts)
{
    std::ranges::fill(counts, 0);
    Device* gpu = device();
    // Small inputs aren't worth a GPU round trip.
    if (gpu != nullptr && bytes.size() >= (std::size_t{1} << 16U))
    {
        auto input = gpu->buffer(bytes.size());
        auto output = gpu->buffer(256 * sizeof(uint32_t));
        if (input && output)
        {
            std::memcpy(input->bytes().data(), bytes.data(), bytes.size());
            std::ranges::fill(output->bytes(), 0);
            const auto size = static_cast<uint32_t>(bytes.size());
            constexpr uint32_t stretch = 256;
            if (gpu->run("histogram", (bytes.size() + stretch - 1) / stretch,
                         {Argument::of(*input), Argument::of(*output), Argument::of_value(size),
                          Argument::of_value(stretch)}))
            {
                std::ranges::copy(output->as<uint32_t>(), counts.begin());
                return;
            }
        }
    }
    for (const uint8_t byte : bytes)
    {
        ++counts[byte];
    }
}

bool lzt_matches(std::span<const uint8_t> bytes, std::span<const uint32_t> previous, unsigned depth,
                 unsigned nice, unsigned good, unsigned slots,
                 std::span<std::pair<uint32_t, uint32_t>> matches, std::span<uint8_t> counts)
{
    Device* gpu = device();
    if (gpu == nullptr)
    {
        return false;
    }
    const std::size_t n = bytes.size();
    auto input = gpu->buffer(n);
    auto chains = gpu->buffer(n * sizeof(uint32_t));
    auto pairs = gpu->buffer(n * slots * 2 * sizeof(uint32_t));
    auto numbers = gpu->buffer(n);
    if (!input || !chains || !pairs || !numbers)
    {
        return false;
    }
    std::memcpy(input->bytes().data(), bytes.data(), n);
    std::memcpy(chains->bytes().data(), previous.data(), n * sizeof(uint32_t));
    const auto size = static_cast<uint32_t>(n);
    if (!gpu->run("lzt_matches", n,
                  {Argument::of(*input), Argument::of(*chains), Argument::of(*pairs),
                   Argument::of(*numbers), Argument::of_value(size), Argument::of_value(depth),
                   Argument::of_value(nice), Argument::of_value(good), Argument::of_value(slots)}))
    {
        return false;
    }
    const std::span<const uint32_t> raw = pairs->as<uint32_t>();
    for (std::size_t i = 0; i < n * slots; ++i)
    {
        matches[i] = {raw[2 * i], raw[(2 * i) + 1]};
    }
    std::memcpy(counts.data(), numbers->bytes().data(), n);
    return true;
}

} // namespace compressor::gpu
