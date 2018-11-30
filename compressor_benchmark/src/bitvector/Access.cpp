#include <thread>
#include <iostream>

#include <benchmark/benchmark.h>
#include <boost/dynamic_bitset/dynamic_bitset.hpp>

#include <bitvector/bitvector.h>

static void BM_BoolVectorAccess(benchmark::State& state) {
    uint8_t bit_count = state.range(0);
    std::size_t index = state.range(1);
    std::vector<bool> sample(bit_count);
    for (auto _ : state)
        sample[index];
}

BENCHMARK(BM_BoolVectorAccess)->Ranges({{16, 32}, {8, 15}});

static void BM_BoostDynamicBitSetAccess(benchmark::State& state) {
    uint8_t bit_count = state.range(0);
    std::size_t index = state.range(1);
    boost::dynamic_bitset<> sample(bit_count);
    for (auto _ : state)
        sample[index];
}

BENCHMARK(BM_BoostDynamicBitSetAccess)->Ranges({{16, 32}, {8, 15}});

static void BM_BitVectorAccess(benchmark::State& state) {
    uint8_t bit_count = state.range(0);
    std::size_t index = state.range(1);
    BitVector sample(bit_count);
    for (auto _ : state)
        sample[index];
}

BENCHMARK(BM_BitVectorAccess)->Ranges({{16, 32}, {8, 15}});
