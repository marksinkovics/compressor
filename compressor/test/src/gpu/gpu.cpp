#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/gpu/gpu.h>

namespace
{

std::vector<uint8_t> noise(std::size_t size, uint32_t seed)
{
    std::vector<uint8_t> bytes(size);
    for (uint8_t& byte : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        byte = static_cast<uint8_t>(seed >> 16U);
    }
    return bytes;
}

std::array<uint32_t, 256> cpu_histogram(std::span<const uint8_t> bytes)
{
    std::array<uint32_t, 256> counts{};
    for (const uint8_t byte : bytes)
    {
        ++counts[byte];
    }
    return counts;
}

} // namespace

TEST(GpuTests, The_histogram_matches_the_cpu_at_every_size)
{
    for (const std::size_t size :
         {std::size_t{0}, std::size_t{1}, std::size_t{65535}, std::size_t{65536},
          std::size_t{65537}, std::size_t{1000003}, std::size_t{1} << 24U})
    {
        std::vector<uint8_t> bytes = noise(size, static_cast<uint32_t>(size));
        if (size > 10)
        {
            std::memset(bytes.data(), 'a', size / 3); // skewed, so atomics contend
        }
        std::array<uint32_t, 256> counts{};
        compressor::gpu::histogram(bytes, counts);
        EXPECT_EQ(counts, cpu_histogram(bytes)) << size;
    }
}

TEST(GpuTests, Buffers_are_shared_and_unknown_kernels_fail)
{
    compressor::gpu::Device* gpu = compressor::gpu::device();
    if (gpu == nullptr)
    {
        GTEST_SKIP() << "no GPU (or COMPRESSOR_METAL off, or COMPRESSOR_NO_GPU set)";
    }
    EXPECT_FALSE(gpu->name().empty());
    auto buffer = gpu->buffer(1024);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->bytes().size(), 1024U);
    buffer->bytes()[1023] = 7;
    EXPECT_EQ(buffer->as<uint32_t>().size(), 256U);
    EXPECT_EQ(buffer->as<uint8_t>()[1023], 7);
    EXPECT_FALSE(gpu->run("no_such_kernel", 1, {compressor::gpu::Argument::of(*buffer)}));
}
