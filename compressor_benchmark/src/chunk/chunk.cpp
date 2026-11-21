#include "chunk.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <benchmark/benchmark.h>

#include <compressor/chunk/chunk.h>

namespace compressor::bench
{

namespace
{

std::vector<uint8_t> noise(std::size_t size)
{
    std::vector<uint8_t> bytes(size);
    uint32_t seed = 1;
    for (uint8_t& byte : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        byte = static_cast<uint8_t>(seed >> 16U);
    }
    return bytes;
}

void chunk(::benchmark::State& state, bool hash)
{
    const std::vector<uint8_t> bytes = noise(std::size_t{64} << 20U);
    const auto average = static_cast<std::size_t>(state.range(0));
    for (auto _ : state)
    {
        const std::vector<std::size_t> ends = compressor::chunk_ends(bytes, average);
        uint64_t sum = 0;
        std::size_t start = 0;
        for (const std::size_t end : ends)
        {
            if (hash)
            {
                sum += compressor::chunk_hash(std::span(bytes).subspan(start, end - start));
            }
            start = end;
        }
        ::benchmark::DoNotOptimize(sum);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(bytes.size()));
}

} // namespace

void register_chunk_benchmarks()
{
    ::benchmark::RegisterBenchmark("Chunk/ends", chunk, false)
        ->Arg(512)
        ->Arg(4096)
        ->Unit(::benchmark::kMillisecond);
    ::benchmark::RegisterBenchmark("Chunk/ends_and_hashes", chunk, true)
        ->Arg(512)
        ->Arg(4096)
        ->Unit(::benchmark::kMillisecond);
}

} // namespace compressor::bench
