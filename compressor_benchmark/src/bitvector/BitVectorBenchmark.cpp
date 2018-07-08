#include <thread>
#include <benchmark/benchmark.h>
#include <Bitvector.hpp>

static void BM_BoolVectorCreation(benchmark::State& state) {
    int bit_count = state.range(0) * 8;
    for (auto _ : state)
        std::vector<bool> sample(bit_count);
}

BENCHMARK(BM_BoolVectorCreation)->RangeMultiplier(2)->Range(8, 1<<10);

static void BM_BitVectorCreation(benchmark::State& state) {
    size_t byte_count = state.range(0);
    for (auto _ : state)
        BitVector sample(byte_count);
}

BENCHMARK(BM_BitVectorCreation)->RangeMultiplier(2)->Range(8, 1<<10);
