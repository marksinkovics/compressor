#include "gpu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <compressor/gpu/gpu.h>

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

// The plumbing's cost: a byte histogram, GPU (copy into a shared buffer included) against CPU.
void histogram(::benchmark::State& state, bool gpu)
{
    const std::vector<uint8_t> bytes = noise(static_cast<std::size_t>(state.range(0)));
    std::array<uint32_t, 256> counts{};
    for (auto _ : state)
    {
        if (gpu)
        {
            compressor::gpu::histogram(bytes, counts);
        }
        else
        {
            counts.fill(0);
            for (const uint8_t byte : bytes)
            {
                ++counts[byte];
            }
        }
        ::benchmark::DoNotOptimize(counts);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}

} // namespace

void register_gpu_benchmarks()
{
    if (compressor::gpu::device() == nullptr)
    {
        return;
    }
    for (const bool gpu : {false, true})
    {
        ::benchmark::RegisterBenchmark(std::string("Gpu/histogram/") + (gpu ? "gpu" : "cpu"),
                                       histogram, gpu)
            ->Arg(1'048'576)  // 1 MiB
            ->Arg(67'108'864) // 64 MiB
            // The calling thread mostly waits for the GPU: its CPU time would flatter it.
            ->UseRealTime()
            ->Unit(::benchmark::kMillisecond);
    }
}

} // namespace compressor::bench
