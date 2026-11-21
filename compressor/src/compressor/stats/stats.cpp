#include <compressor/stats/stats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <ios>
#include <istream>
#include <memory>
#include <numeric>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <compressor/archive/index.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/huffman/huffman.h>
#include <compressor/longrange/longrange.h>

namespace compressor
{

namespace
{

void count(Frequencies& frequencies, const uint8_t* bytes, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        ++frequencies[bytes[i]];
    }
}

std::expected<void, Error> count_raw(std::istream& in, Stats& stats)
{
    in.clear();
    in.seekg(0);
    std::vector<char> buffer(std::size_t{1} << 16U);
    while (in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        count(stats.frequencies, reinterpret_cast<const uint8_t*>(buffer.data()), got);
        stats.content_bytes += got;
    }
    if (in.bad())
    {
        return std::unexpected(Error::read_failed);
    }
    return {};
}

std::expected<ArchiveCounts, Error> count_entries(std::istream& in)
{
    in.clear();
    in.seekg(0);
    const auto info = read_archive_index(in);
    if (!info)
    {
        return std::unexpected(info.error());
    }
    ArchiveCounts counts;
    uint64_t stored_end = 0;
    for (const Entry& entry : info->index.entries)
    {
        switch (entry.type)
        {
        case EntryType::file:
            ++counts.files;
            if (entry.size > 0 && entry.offset < stored_end)
            {
                ++counts.duplicates;
                counts.duplicate_bytes += entry.size;
            }
            stored_end = std::max(stored_end, entry.offset + entry.size);
            break;
        case EntryType::directory:
            ++counts.directories;
            break;
        case EntryType::symlink:
            ++counts.symlinks;
            break;
        }
    }
    return counts;
}

std::string block_size_text(uint8_t log2)
{
    return log2 >= 20 ? std::format("{} MiB", uint64_t{1} << (log2 - 20U))
                      : std::format("{} KiB", uint64_t{1} << (log2 - 10U));
}

std::string plural(uint64_t count, const char* one, const char* many)
{
    return std::format("{} {}", count, count == 1 ? one : many);
}

std::string_view method_name(Method method)
{
    switch (method)
    {
    case Method::stored:
        return "stored";
    case Method::huffman:
        return "huffman";
    case Method::pipeline:
        return "pipeline";
    case Method::references:
        return "references";
    }
    return "unknown";
}

uint64_t bits_to_bytes(double bits)
{
    return static_cast<uint64_t>(std::ceil(bits / 8));
}

} // namespace

std::expected<Stats, Error> collect_stats(std::istream& in,
                                          std::shared_ptr<const Dictionary> dictionary)
{
    Stats stats;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0)
    {
        return std::unexpected(Error::read_failed);
    }
    stats.input_bytes = static_cast<uint64_t>(size);
    in.seekg(0);

    FrameReader reader(in);
    const auto header = reader.read_header();
    if (!header)
    {
        if (header.error() != Error::not_ingot)
        {
            return std::unexpected(header.error());
        }
        if (auto counted = count_raw(in, stats); !counted)
        {
            return std::unexpected(counted.error());
        }
        return stats;
    }

    stats.ingot = true;
    stats.block_size_log2 = *header;
    Engine engine(*header, 1, std::move(dictionary));
    std::optional<LongRangeHistory> history;
    if ((reader.flags() & long_range_flag) != 0)
    {
        history.emplace(window_blocks(*header, reader.window_log2()));
    }
    EncodedBlock block;
    std::vector<uint8_t> bytes;
    while (true)
    {
        const auto more = reader.read_block(block);
        if (!more)
        {
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            break;
        }
        bytes.clear();
        if (auto decoded =
                history ? history->decode(engine, block, bytes) : engine.decode_block(block, bytes);
            !decoded)
        {
            return std::unexpected(decoded.error());
        }
        count(stats.frequencies, bytes.data(), bytes.size());
        stats.content_bytes += bytes.size();
        stats.blocks.push_back(
            {.method = block.method_, .size = block.size_, .encoded_bytes = written_size(block)});
    }
    if ((reader.flags() & archive_flag) != 0)
    {
        auto counts = count_entries(in);
        if (!counts)
        {
            return std::unexpected(counts.error());
        }
        stats.archive = *counts;
    }
    return stats;
}

double entropy_bits_per_byte(const Frequencies& frequencies)
{
    const uint64_t total = std::accumulate(frequencies.begin(), frequencies.end(), uint64_t{0});
    double entropy = 0;
    for (const uint64_t frequency : frequencies)
    {
        if (frequency != 0)
        {
            const double p = static_cast<double>(frequency) / static_cast<double>(total);
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

uint64_t huffman_bits(const Frequencies& frequencies)
{
    const CodeLengths lengths = code_lengths(frequencies);
    uint64_t bits = 0;
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol)
    {
        bits += frequencies[symbol] * lengths[symbol];
    }
    return bits;
}

void print_stats(const Stats& stats, const StatsReport& report, std::ostream& out)
{
    std::string kind = "not an ingot file";
    if (stats.archive)
    {
        kind = std::format("ingot folder archive with {}, {} and {}",
                           plural(stats.archive->files, "file", "files"),
                           plural(stats.archive->directories, "directory", "directories"),
                           plural(stats.archive->symlinks, "symlink", "symlinks"));
        if (stats.archive->duplicates > 0)
        {
            kind += std::format("; {} of {} stored once",
                                plural(stats.archive->duplicates, "duplicate", "duplicates"),
                                plural(stats.archive->duplicate_bytes, "byte", "bytes"));
        }
    }
    else if (stats.ingot)
    {
        kind = "ingot file";
    }
    out << std::format("Input:    {} bytes, {}\n", stats.input_bytes, kind);

    if (stats.ingot)
    {
        const auto count = [&stats](Method method) {
            return std::ranges::count(stats.blocks, method, &BlockStats::method);
        };
        out << std::format("Blocks:   {} of {}: {} huffman, {} stored", stats.blocks.size(),
                           block_size_text(stats.block_size_log2), count(Method::huffman),
                           count(Method::stored));
        if (count(Method::pipeline) > 0)
        {
            out << std::format(", {} pipeline", count(Method::pipeline));
        }
        if (count(Method::references) > 0)
        {
            out << std::format(", {} with long-range references", count(Method::references));
        }
        out << '\n';
        if (stats.content_bytes > 0 && stats.input_bytes > 0)
        {
            out << std::format("Ratio:    {:.3f} (the input is {:.1f}% of the content)\n",
                               static_cast<double>(stats.content_bytes) /
                                   static_cast<double>(stats.input_bytes),
                               100.0 * static_cast<double>(stats.input_bytes) /
                                   static_cast<double>(stats.content_bytes));
        }
    }

    const auto distinct = std::ranges::count_if(stats.frequencies, [](uint64_t frequency) {
        return frequency != 0;
    });
    out << std::format("Content:  {} bytes, {} distinct byte values\n", stats.content_bytes,
                       distinct);
    if (stats.content_bytes > 0)
    {
        const double entropy = entropy_bits_per_byte(stats.frequencies);
        const auto content = static_cast<double>(stats.content_bytes);
        const auto huffman = static_cast<double>(huffman_bits(stats.frequencies));
        out << std::format(
            "Entropy:  {:.3f} bits/byte, so at least {} bytes for an order-0 coder\n", entropy,
            bits_to_bytes(entropy * content));
        out << std::format("Huffman:  {:.3f} bits/byte with one code for everything: {} bytes "
                           "plus the code table\n",
                           huffman / content, bits_to_bytes(huffman));
    }

    if (report.blocks && !stats.blocks.empty())
    {
        out << "\nBlock  Method      Decoded     Encoded   Ratio\n";
        for (std::size_t i = 0; i < stats.blocks.size(); ++i)
        {
            const BlockStats& block = stats.blocks[i];
            out << std::format("{:>5}  {:<7} {:>11} {:>11} {:>7.3f}\n", i,
                               method_name(block.method), block.size, block.encoded_bytes,
                               static_cast<double>(block.size) /
                                   static_cast<double>(block.encoded_bytes));
        }
    }

    if (report.histogram && stats.content_bytes > 0)
    {
        std::array<uint8_t, symbol_count> order{};
        std::ranges::iota(order, uint8_t{0});
        std::ranges::stable_sort(order, [&](uint8_t a, uint8_t b) {
            return stats.frequencies[a] > stats.frequencies[b];
        });
        out << "\nByte          Count   Share\n";
        for (const uint8_t byte : order)
        {
            const uint64_t frequency = stats.frequencies[byte];
            if (frequency == 0)
            {
                break;
            }
            const bool printable = byte >= 0x20 && byte < 0x7F;
            out << std::format(
                "0x{:02X} {:<3} {:>10} {:>6.2f}%\n", byte,
                printable ? std::format("'{}'", static_cast<char>(byte)) : "", frequency,
                100.0 * static_cast<double>(frequency) / static_cast<double>(stats.content_bytes));
        }
    }
}

void write_content_graph(const Stats& stats, std::ostream& out)
{
    write_code_graph(out, canonical_codes(code_lengths(stats.frequencies)), stats.frequencies);
}

} // namespace compressor
