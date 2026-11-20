#ifndef CompressorBenchmarkEntropy_h
#define CompressorBenchmarkEntropy_h

namespace compressor::bench
{

// Order-0 tANS against order-0 rANS per 1 MiB block on Silesia files, raw and after a delta:
// size, encoding and decoding speed.
void register_entropy_benchmarks();

} // namespace compressor::bench

#endif /* CompressorBenchmarkEntropy_h */
