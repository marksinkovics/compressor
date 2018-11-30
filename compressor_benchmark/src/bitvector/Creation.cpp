#include <thread>

#include <benchmark/benchmark.h>
#include <boost/dynamic_bitset/dynamic_bitset.hpp>

#include <bitvector/bitvector.h>

static void BM_BoolVectorCreation(benchmark::State& state) {
    uint8_t bit_count = state.range(0);
    for (auto _ : state)
        std::vector<bool> sample(bit_count);
}

BENCHMARK(BM_BoolVectorCreation)->RangeMultiplier(2)->Range(8, 1<<10);

static void BM_BoostDynamicBitSetCreation(benchmark::State& state) {
    uint8_t bit_count = state.range(0);
    for (auto _ : state)
        boost::dynamic_bitset<> sample(bit_count);
}

BENCHMARK(BM_BoostDynamicBitSetCreation)->RangeMultiplier(2)->Range(8, 1<<10);

static void BM_BitVectorCreation(benchmark::State& state) {
    uint8_t bit_count = state.range(0);
    for (auto _ : state)
        BitVector sample(bit_count);
}

BENCHMARK(BM_BitVectorCreation)->RangeMultiplier(2)->Range(8, 1<<10);
