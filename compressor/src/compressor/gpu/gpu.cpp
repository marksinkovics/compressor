#include <compressor/gpu/gpu.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

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

} // namespace compressor::gpu
