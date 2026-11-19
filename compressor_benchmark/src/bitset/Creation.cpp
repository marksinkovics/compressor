#include <thread>

#include <benchmark/benchmark.h>
#include <boost/dynamic_bitset/dynamic_bitset.hpp>

#include <compressor/bitset/bitset.h>

static void BM_BoolVectorCreation(benchmark::State& state)
{
    auto bit_count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state)
        std::vector<bool> sample(bit_count);
}

BENCHMARK(BM_BoolVectorCreation)->RangeMultiplier(2)->Range(8, 1024);

static void BM_BoostDynamicBitSetCreation(benchmark::State& state)
{
    auto bit_count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state)
        boost::dynamic_bitset<> sample(bit_count);
}

BENCHMARK(BM_BoostDynamicBitSetCreation)->RangeMultiplier(2)->Range(8, 1024);

static void BM_BitSetCreation(benchmark::State& state)
{
    auto bit_count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state)
        compressor::bitset sample(bit_count);
}

BENCHMARK(BM_BitSetCreation)->RangeMultiplier(2)->Range(8, 1024);
