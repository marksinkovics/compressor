#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <compressor/archive/archive.h>
#include <compressor/archive/metadata.h>
#include <compressor/data/data.h>
#include <compressor/deflate/deflate.h>
#include <compressor/engine/engine.h>
#include <compressor/stream/encoder.h>

namespace
{

namespace fs = std::filesystem;

// Small blocks, so a few KiB give several blocks for the block table and range decoding.
constexpr uint8_t block_size_log2 = compressor::min_block_size_log2;

// Level 6 gives LZ77 + Huffman pipeline blocks; level 1 only stored and Huffman ones.
compressor::BlockEncoder make_encoder(unsigned level = 6)
{
    return {[level] {
        return std::make_unique<compressor::Engine>(block_size_log2, level);
    }, block_size_log2, 1};
}

std::string text(std::size_t size)
{
    std::string bytes(size, '\0');
    uint32_t state = 3;
    for (char& c : bytes)
    {
        state = (state * 1103515245U) + 12345U;
        c = "eeeettaaoinshr \n"[(state >> 16U) % 16];
    }
    return bytes;
}

// Repeated words, so LZ77 finds matches.
std::string words(std::size_t size)
{
    static constexpr std::array<std::string_view, 6> vocabulary{"index ", "block ",   "archive\n",
                                                                "match ", "huffman ", "seed, "};
    std::string bytes;
    uint32_t state = 7;
    while (bytes.size() < size)
    {
        state = (state * 1103515245U) + 12345U;
        bytes += vocabulary[(state >> 16U) % vocabulary.size()];
    }
    bytes.resize(size);
    return bytes;
}

// Little-endian 16-bit samples of a slow wave, which get a delta transform.
std::string samples(std::size_t count)
{
    std::string bytes;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto value = static_cast<uint16_t>(30'000 + ((i * 37) % 2000));
        bytes.push_back(static_cast<char>(value & 0xFFU));
        bytes.push_back(static_cast<char>(value >> 8U));
    }
    return bytes;
}

// 16-bit samples taking wide random steps, which get a delta and the rANS coder.
std::string random_walk(std::size_t count)
{
    std::string bytes;
    uint32_t state = 5;
    int32_t value = 30'000;
    for (std::size_t i = 0; i < count; ++i)
    {
        state = (state * 1103515245U) + 12345U;
        value += static_cast<int32_t>((state >> 16U) % 1023) - 511;
        bytes.push_back(static_cast<char>(static_cast<uint16_t>(value)));
        bytes.push_back(static_cast<char>(static_cast<uint16_t>(value) >> 8U));
    }
    return bytes;
}

// Binary records (a counter, a field from a few values, random bytes), which take the LZR coder.
std::string records(std::size_t count)
{
    std::string bytes;
    uint32_t state = 3;
    for (std::size_t id = 0; id < count; ++id)
    {
        state = (state * 1103515245U) + 12345U;
        for (unsigned i = 0; i < 4; ++i)
        {
            bytes.push_back(static_cast<char>(id >> (8 * i)));
        }
        const uint32_t kind = (state >> 16U) % 5;
        for (unsigned i = 0; i < 12; ++i)
        {
            bytes.push_back(static_cast<char>((kind * 37) + i));
        }
        for (unsigned i = 0; i < 8; ++i)
        {
            state = (state * 1103515245U) + 12345U;
            bytes.push_back(static_cast<char>(state >> 16U));
        }
    }
    return bytes;
}

std::string noise(std::size_t size)
{
    std::string bytes(size, '\0');
    uint32_t state = 5;
    for (char& c : bytes)
    {
        state = (state * 1103515245U) + 12345U;
        c = static_cast<char>(state >> 16U);
    }
    return bytes;
}

void write(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::out | std::ios::binary | std::ios::trunc) << bytes;
}

std::string encode(const std::string& input, unsigned level = 6)
{
    auto encoder = make_encoder(level);
    std::istringstream in(input);
    std::ostringstream out;
    if (!encoder.encode(in, out))
    {
        std::cerr << "encoding failed\n";
        std::exit(1);
    }
    return out.str();
}

std::string archive(const fs::path& scratch, bool search_index = false)
{
    fs::remove_all(scratch);
    fs::create_directories(scratch / "docs" / "deep");
    fs::create_directories(scratch / "empty");
    write(scratch / "a.txt", words(3000));
    write(scratch / "docs" / "b.bin", noise(5000));
    write(scratch / "docs" / "deep" / "c.txt", text(100));
    write(scratch / "docs" / "zero", "");
    write(scratch / "docs" / "copy.txt", words(3000)); // a duplicate of a.txt
    fs::create_symlink("a.txt", scratch / "link");
    // Fixed times, so the seeds come out the same on every run.
    for (const auto& entry : fs::recursive_directory_iterator(scratch))
    {
        if (compressor::set_mtime(entry.path(), 1'000'000'000'000'000'000))
        {
            std::cerr << "cannot set the time of " << entry.path().string() << '\n';
            std::exit(1);
        }
    }

    std::ostringstream warnings;
    const auto entries = compressor::scan_tree(scratch, {}, warnings);
    auto encoder = make_encoder();
    std::ostringstream out;
    if (!entries || !compressor::write_archive(encoder, scratch, *entries, out, search_index))
    {
        std::cerr << "archiving failed\n";
        std::exit(1);
    }
    std::error_code error;
    fs::remove_all(scratch, error);
    return out.str();
}

// The size the inflate target expects, then a raw Deflate stream.
std::string deflate_seed(const std::string& input)
{
    const std::vector<uint8_t> deflated =
        compressor::deflate_bytes({reinterpret_cast<const uint8_t*>(input.data()), input.size()});
    std::string seed{static_cast<char>(input.size() & 0xFFU),
                     static_cast<char>(input.size() >> 8U)};
    seed.append(deflated.begin(), deflated.end());
    return seed;
}

} // namespace

// Writes the seed corpus for each fuzz target into `<dir>/<target>/`.
int main(int argc, char* argv[])
try
{
    if (argc != 2)
    {
        std::cerr << "usage: fuzz_make_seeds <corpus directory>\n";
        return 1;
    }
    const fs::path dir(argv[1]);
    const std::string plain = encode(words(10'000));
    const std::string huffman = encode(text(10'000), 1);
    const std::string stored = encode(noise(5000));
    const std::string empty = encode("");
    const std::string folder = archive(dir / ".scratch");

    write(dir / "container" / "text.ingot", plain);
    write(dir / "container" / "huffman.ingot", huffman);
    {
        std::istringstream in(words(3000));
        std::ostringstream gzip;
        (void)compressor::write_gzip(in, gzip, {.name = "seed.txt", .mtime = 1}).has_value();
        write(dir / "container" / "seed.gz", gzip.str());
    }
    write(dir / "container" / "samples.ingot", encode(samples(3000)));
    write(dir / "container" / "bwt.ingot", encode(words(6000), 9));
    write(dir / "container" / "walk.ingot", encode(random_walk(3000)));
    write(dir / "container" / "records.ingot", encode(records(400), 9));
    write(dir / "inflate" / "text.deflate", deflate_seed(words(5000)));
    write(dir / "inflate" / "noise.deflate", deflate_seed(noise(700)));
    write(dir / "container" / "stored.ingot", stored);
    write(dir / "container" / "empty.ingot", empty);
    write(dir / "container" / "archive.ingot", folder);
    write(dir / "archive" / "archive.ingot", folder);
    write(dir / "archive" / "indexed.ingot", archive(dir / ".scratch", true));
    write(dir / "archive" / "text.ingot", plain);
    write(dir / "stats" / "text.ingot", plain);
    write(dir / "stats" / "archive.ingot", folder);
    write(dir / "stats" / "raw.txt", text(2000));
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}
