#include "codec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>
#include <zlib.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>

namespace compressor::bench
{

namespace
{

namespace fs = std::filesystem;

using Bytes = std::vector<uint8_t>;

std::vector<fs::path> files_in(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(dir, error))
    {
        if (entry.is_regular_file(error) && !entry.path().filename().string().starts_with('.'))
        {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

Bytes read_file(const fs::path& path)
{
    Bytes bytes(fs::file_size(path));
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

// Order-0 entropy of the whole input, in bits.
double entropy_bits(const Bytes& bytes)
{
    std::array<uint64_t, 256> counts{};
    for (const uint8_t byte : bytes)
    {
        ++counts[byte];
    }
    const auto total = static_cast<double>(bytes.size());
    double bits = 0.0;
    for (const uint64_t count : counts)
    {
        if (count > 0)
        {
            const double p = static_cast<double>(count) / total;
            bits -= static_cast<double>(count) * std::log2(p);
        }
    }
    return bits;
}

std::size_t serialized_size(const EncodedData& encoded)
{
    std::ostringstream stream(std::ios::out | std::ios::binary);
    stream << encoded;
    return stream.str().size();
}

Bytes zlib_compress(const Bytes& input)
{
    z_stream stream{};
    if (deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_HUFFMAN_ONLY) != Z_OK)
    {
        return {};
    }
    Bytes output(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = deflate(&stream, Z_FINISH);
    output.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END ? output : Bytes{};
}

Bytes zlib_decompress(const Bytes& input, std::size_t original_size)
{
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK)
    {
        return {};
    }
    Bytes output(original_size);
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = inflate(&stream, Z_FINISH);
    output.resize(stream.total_out);
    inflateEnd(&stream);
    return result == Z_STREAM_END ? output : Bytes{};
}

// One file at a time: benchmarks are registered file by file, so the encode benchmark's output
// is still here when the decode benchmark of the same file runs. Silesia files are up to 51 MB.
struct Prepared
{
    fs::path path;
    Bytes input;
    double entropy = 0.0;
    std::optional<EncodedData> engine_encoded;
    std::optional<Bytes> zlib_encoded;
};

Prepared& prepare(const fs::path& path)
{
    static Prepared prepared;
    if (prepared.path != path)
    {
        prepared = Prepared{};
        prepared.path = path;
        prepared.input = read_file(path);
        prepared.entropy = entropy_bits(prepared.input);
    }
    return prepared;
}

void set_size_counters(::benchmark::State& state, const Prepared& file, std::size_t compressed,
                       std::optional<std::size_t> header)
{
    const auto original = static_cast<double>(file.input.size());
    state.counters["ratio"] = static_cast<double>(compressed) / original;
    if (header)
    {
        state.counters["header"] = static_cast<double>(*header);
    }
    if (file.entropy > 0.0)
    {
        state.counters["efficiency"] = static_cast<double>(compressed) * 8.0 / file.entropy;
    }
}

void set_bytes_processed(::benchmark::State& state, const Prepared& file)
{
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(file.input.size()));
}

DecodedData as_decoded(const Bytes& bytes)
{
    DecodedData data;
    data.data_ = bytes;
    return data;
}

void engine_encode(::benchmark::State& state, const fs::path& path)
{
    Prepared& file = prepare(path);
    const DecodedData input = as_decoded(file.input);
    EncodedData encoded;
    for (auto _ : state)
    {
        Engine engine;
        encoded = engine.encode(input);
        ::benchmark::DoNotOptimize(encoded);
    }
    set_bytes_processed(state, file);
    const std::size_t total = serialized_size(encoded);
    set_size_counters(state, file, total, total - encoded.data_.num_bytes());
    file.engine_encoded = std::move(encoded);
}

void engine_decode(::benchmark::State& state, const fs::path& path)
{
    Prepared& file = prepare(path);
    if (!file.engine_encoded)
    {
        Engine engine;
        file.engine_encoded = engine.encode(as_decoded(file.input));
    }
    DecodedData decoded;
    for (auto _ : state)
    {
        Engine engine;
        decoded = engine.decode(*file.engine_encoded);
        ::benchmark::DoNotOptimize(decoded);
    }
    set_bytes_processed(state, file);
    if (decoded.data_ != file.input)
    {
        state.SkipWithError("decoded data differs from the input");
    }
}

void zlib_encode(::benchmark::State& state, const fs::path& path)
{
    Prepared& file = prepare(path);
    Bytes encoded;
    for (auto _ : state)
    {
        encoded = zlib_compress(file.input);
        ::benchmark::DoNotOptimize(encoded);
    }
    set_bytes_processed(state, file);
    if (encoded.empty() && !file.input.empty())
    {
        state.SkipWithError("deflate failed");
        return;
    }
    set_size_counters(state, file, encoded.size(), std::nullopt);
    file.zlib_encoded = std::move(encoded);
}

void zlib_decode(::benchmark::State& state, const fs::path& path)
{
    Prepared& file = prepare(path);
    if (!file.zlib_encoded)
    {
        file.zlib_encoded = zlib_compress(file.input);
    }
    Bytes decoded;
    for (auto _ : state)
    {
        decoded = zlib_decompress(*file.zlib_encoded, file.input.size());
        ::benchmark::DoNotOptimize(decoded);
    }
    set_bytes_processed(state, file);
    if (decoded != file.input)
    {
        state.SkipWithError("inflated data differs from the input");
    }
}

std::string benchmark_name(const fs::path& file)
{
    std::string name = file.filename().string();
    std::ranges::replace_if(name, [](unsigned char c) {
        return std::isalnum(c) == 0;
    }, '_');
    return name;
}

} // namespace

void register_codec_benchmarks()
{
    using Benchmark = void (*)(::benchmark::State&, const fs::path&);
    const std::array<std::pair<const char*, Benchmark>, 4> codecs{{
        {"Engine/Encode", engine_encode},
        {"Engine/Decode", engine_decode},
        {"ZlibHuffman/Encode", zlib_encode},
        {"ZlibHuffman/Decode", zlib_decode},
    }};
    const std::array<std::pair<const char*, fs::path>, 3> sets{{
        {"samples", COMPRESSOR_SAMPLES_DIR},
        {"canterbury", fs::path(COMPRESSOR_CORPUS_DIR) / "canterbury"},
        {"silesia", fs::path(COMPRESSOR_CORPUS_DIR) / "silesia"},
    }};

    for (const auto& [set, dir] : sets)
    {
        const std::vector<fs::path> files = files_in(dir);
        if (files.empty())
        {
            std::cerr << "No files in " << dir.string()
                      << ", skipping; run tools/fetch-corpus.sh to download the corpora\n";
            continue;
        }
        for (const fs::path& file : files)
        {
            for (const auto& [codec, function] : codecs)
            {
                const std::string name =
                    std::string(codec) + "/" + set + "/" + benchmark_name(file);
                ::benchmark::RegisterBenchmark(name, function, file)
                    ->Unit(::benchmark::kMillisecond);
            }
        }
    }
}

} // namespace compressor::bench
