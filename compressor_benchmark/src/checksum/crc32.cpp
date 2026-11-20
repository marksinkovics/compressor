#include "crc32.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <benchmark/benchmark.h>

#include <compressor/checksum/crc32.h>

namespace compressor::bench
{

namespace
{

std::vector<uint8_t> noise(std::size_t size)
{
    std::vector<uint8_t> bytes(size);
    uint32_t state = 1;
    for (uint8_t& byte : bytes)
    {
        state = (state * 1103515245U) + 12345U;
        byte = static_cast<uint8_t>(state >> 16U);
    }
    return bytes;
}

template <uint32_t (*Crc)(std::span<const uint8_t>, uint32_t)>
void crc32_bench(::benchmark::State& state)
{
    const std::vector<uint8_t> bytes = noise(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state)
    {
        ::benchmark::DoNotOptimize(Crc(bytes, 0));
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}

} // namespace

void register_crc32_benchmarks()
{
    // A block, and a size that isn't a multiple of anything the loops use.
    for (const int64_t size : {int64_t{1'048'576}, int64_t{100'003}})
    {
        ::benchmark::RegisterBenchmark("Crc32/Dispatched", crc32_bench<crc32>)->Arg(size);
        ::benchmark::RegisterBenchmark("Crc32/Portable", crc32_bench<crc32_portable>)->Arg(size);
    }
}

} // namespace compressor::bench
