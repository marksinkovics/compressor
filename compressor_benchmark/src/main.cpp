#include <benchmark/benchmark.h>

#include "checksum/crc32.h"
#include "codec/codec.h"
#include "search/search.h"

int main(int argc, char** argv)
{
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
    {
        return 1;
    }
    compressor::bench::register_codec_benchmarks();
    compressor::bench::register_crc32_benchmarks();
    compressor::bench::register_search_benchmarks();
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
