#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/chunk/chunk.h>

namespace
{

using Bytes = std::vector<uint8_t>;

Bytes noise(std::size_t size, uint32_t seed)
{
    Bytes bytes(size);
    for (uint8_t& byte : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        byte = static_cast<uint8_t>(seed >> 16U);
    }
    return bytes;
}

} // namespace

TEST(ChunkTests, Chunks_cover_the_input_within_their_size_limits)
{
    EXPECT_TRUE(compressor::chunk_ends({}, 1024).empty());
    for (const std::size_t average : {std::size_t{64}, std::size_t{1024}, std::size_t{16384}})
    {
        for (const Bytes& input : {noise(300'000, 1), Bytes(300'000, 7), noise(100, 2)})
        {
            const std::vector<std::size_t> ends = compressor::chunk_ends(input, average);
            ASSERT_FALSE(ends.empty());
            EXPECT_EQ(ends.back(), input.size());
            std::size_t start = 0;
            for (std::size_t i = 0; i < ends.size(); ++i)
            {
                const std::size_t size = ends[i] - start;
                EXPECT_LE(size, average * 8);
                if (i + 1 < ends.size())
                {
                    EXPECT_GE(size, average / 4);
                }
                start = ends[i];
            }
        }
        const std::size_t count = compressor::chunk_ends(noise(1'000'000, 3), average).size();
        EXPECT_GT(count, 1'000'000 / (2 * average)) << average;
        EXPECT_LT(count, std::size_t{2'000'000} / average) << average;
    }
}

TEST(ChunkTests, An_insertion_moves_only_the_boundaries_near_it)
{
    const Bytes original = noise(500'000, 4);
    Bytes edited = original;
    const Bytes inserted = noise(37, 5);
    edited.insert(edited.begin() + 1000, inserted.begin(), inserted.end());

    const std::vector<std::size_t> before = compressor::chunk_ends(original, 1024);
    const std::vector<std::size_t> after = compressor::chunk_ends(edited, 1024);
    std::unordered_set<std::size_t> shifted;
    for (const std::size_t end : after)
    {
        shifted.insert(end - std::min(end, inserted.size()));
    }
    std::size_t same = 0;
    for (const std::size_t end : before)
    {
        same += static_cast<std::size_t>(end > 1000 && shifted.contains(end));
    }
    EXPECT_GE(same + 3, before.size());
}

TEST(ChunkTests, The_hash_tells_chunks_apart)
{
    const Bytes bytes = noise(4096, 6);
    std::unordered_set<uint64_t> hashes;
    for (std::size_t size = 0; size <= 64; ++size)
    {
        hashes.insert(compressor::chunk_hash(std::span(bytes).first(size)));
    }
    EXPECT_EQ(hashes.size(), 65U);
    Bytes changed = bytes;
    changed[2000] ^= 1U;
    EXPECT_NE(compressor::chunk_hash(changed), compressor::chunk_hash(bytes));
    EXPECT_EQ(compressor::chunk_hash(Bytes(bytes)), compressor::chunk_hash(bytes));
}
