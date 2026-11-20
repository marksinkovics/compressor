#ifndef CompressorBenchmarkCm_h
#define CompressorBenchmarkCm_h

namespace compressor::bench
{

// The CM coder against the BWT coder and LZR per 1 MiB block, on Silesia files.
void register_cm_benchmarks();

} // namespace compressor::bench

#endif /* CompressorBenchmarkCm_h */
