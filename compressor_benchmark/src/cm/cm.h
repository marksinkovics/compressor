#ifndef CompressorBenchmarkCm_h
#define CompressorBenchmarkCm_h

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace compressor::bench
{

// Context mixing experiment (roadmap item 13): bit predictions from hashed order-1 to order-6
// contexts, mixed by a small logistic network and coded with a binary arithmetic coder, in the
// style of lpaq. Not a pipeline coder: decoding is as slow as encoding and the tables need
// 12 MiB per thread.
std::vector<uint8_t> cm_encode(std::span<const uint8_t> bytes);
std::vector<uint8_t> cm_decode(std::span<const uint8_t> coded, std::size_t size);

void register_cm_benchmarks();

} // namespace compressor::bench

#endif /* CompressorBenchmarkCm_h */
