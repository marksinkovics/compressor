#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>

namespace
{

// The bit stream shown is cut here; the totals still count all of it.
constexpr std::size_t max_shown_bits = 16384;
constexpr unsigned max_level = 9;
constexpr std::size_t block_size = std::size_t{1} << compressor::default_block_size_log2;

std::span<const uint8_t> bytes_of(const std::string& text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

std::string coder_name(uint8_t coder)
{
    switch (static_cast<compressor::Coder>(coder))
    {
    case compressor::Coder::lz77_huffman:
        return "LZ77 + Huffman";
    case compressor::Coder::bwt:
        return "BWT";
    case compressor::Coder::rans:
        return "rANS";
    case compressor::Coder::lzr:
        return "LZR";
    case compressor::Coder::cm:
        return "context mixing";
    case compressor::Coder::bwt_cm:
        return "BWT + CM";
    case compressor::Coder::tans:
        return "tANS";
    case compressor::Coder::lz77_huffman_dictionary:
    case compressor::Coder::lzr_dictionary:
    case compressor::Coder::cm_dictionary:
    case compressor::Coder::lz77_dictionary_codes:
        break;
    }
    return "coder " + std::to_string(coder);
}

std::string transform_name(uint8_t id)
{
    switch (id)
    {
    case 1:
        return "delta";
    case 3:
        return "ARM64 branches";
    case 4:
        return "x86 branches";
    case 5:
        return "median predictor";
    default:
        return "transform " + std::to_string(id);
    }
}

std::string method_name(const compressor::EncodedBlock& block)
{
    switch (block.method_)
    {
    case compressor::Method::stored:
        return "stored";
    case compressor::Method::huffman:
        return "Huffman";
    case compressor::Method::pipeline:
        break;
    }
    const std::vector<uint8_t>& payload = block.payload_;
    std::string name;
    for (std::size_t i = 0; i < payload[0]; ++i)
    {
        name += transform_name(payload[1 + (i * compressor::pipeline_transform_bytes)]) + ", ";
    }
    return name + coder_name(payload[1 + (payload[0] * compressor::pipeline_transform_bytes)]);
}

} // namespace

// JSON for the page: each byte's count and canonical code (the block coder's own length-limited
// code), the coded bits, and what the engine writes at every level for the same bytes.
std::string analyze(const std::string& text)
{
    const std::span<const uint8_t> bytes = bytes_of(text);
    compressor::Frequencies frequencies{};
    for (const uint8_t byte : bytes)
    {
        ++frequencies[byte];
    }
    const compressor::CodeLengths lengths = compressor::code_lengths(frequencies);
    const compressor::Codes codes = compressor::canonical_codes(lengths);

    std::string json = R"({"size":)" + std::to_string(bytes.size()) + R"(,"symbols":[)";
    bool first = true;
    uint64_t total_bits = 0;
    for (std::size_t s = 0; s < compressor::symbol_count; ++s)
    {
        if (frequencies[s] == 0)
        {
            continue;
        }
        total_bits += frequencies[s] * codes[s].length;
        std::string code;
        for (unsigned i = codes[s].length; i-- > 0;)
        {
            code.push_back(((codes[s].bits >> i) & 1U) != 0 ? '1' : '0');
        }
        json += (first ? "" : ",") + std::string(R"({"byte":)") + std::to_string(s) +
                R"(,"count":)" + std::to_string(frequencies[s]) + R"(,"code":")" + code + "\"}";
        first = false;
    }
    std::string bits;
    for (const uint8_t byte : bytes)
    {
        if (bits.size() >= max_shown_bits)
        {
            break;
        }
        for (unsigned i = codes[byte].length; i-- > 0;)
        {
            bits.push_back(((codes[byte].bits >> i) & 1U) != 0 ? '1' : '0');
        }
    }
    json += R"(],"totalBits":)" + std::to_string(total_bits) + R"(,"bits":")" + bits +
            R"(","levels":[)";

    for (unsigned level = 1; level <= max_level; ++level)
    {
        compressor::Engine engine(compressor::default_block_size_log2, level);
        uint64_t size = compressor::frame_header_bytes + compressor::end_block_bytes;
        std::string method;
        bool decoded = true;
        for (std::size_t at = 0; at < bytes.size(); at += block_size)
        {
            const auto piece =
                bytes.subspan(at, std::min<std::size_t>(block_size, bytes.size() - at));
            const compressor::EncodedBlock block = engine.encode_block(piece);
            size += compressor::written_size(block);
            method = method.empty() ? method_name(block) : method;
            std::vector<uint8_t> out;
            compressor::Engine decoder;
            decoded = decoded && decoder.decode_block(block, out).has_value() &&
                      std::ranges::equal(out, piece);
        }
        json += (level == 1 ? "" : ",") + std::string(R"({"level":)") + std::to_string(level) +
                R"(,"size":)" + std::to_string(size) + R"(,"method":")" +
                (method.empty() ? "none" : method) + R"(","decoded":)" +
                (decoded ? "true" : "false") + "}";
    }
    return json + "]}";
}

// Decodes an ingot file (a Uint8Array) made by any build, checking every block's CRC32: JSON with
// "ok", and "size" or "error".
std::string decode(const emscripten::val& file)
{
    const auto bytes = emscripten::convertJSArrayToNumberVector<uint8_t>(file);
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    const auto encoded = compressor::read_encoded(in);
    if (!encoded)
    {
        return R"({"ok":false,"error":")" + std::string(compressor::describe(encoded.error())) +
               "\"}";
    }
    compressor::Engine engine;
    const auto decoded = engine.decode(*encoded);
    if (!decoded)
    {
        return R"({"ok":false,"error":")" + std::string(compressor::describe(decoded.error())) +
               "\"}";
    }
    return R"({"ok":true,"size":)" + std::to_string(decoded->data_.size()) + "}";
}

EMSCRIPTEN_BINDINGS(ingot_demo)
{
    emscripten::function("analyze", &analyze);
    emscripten::function("decode", &decode);
}
