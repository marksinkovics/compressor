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
#include <system_error>

#include <compressor/archive/archive.h>
#include <compressor/archive/metadata.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/stream/encoder.h>

namespace
{

namespace fs = std::filesystem;

// Small blocks, so a few KiB give several blocks for the block table and range decoding.
constexpr uint8_t block_size_log2 = compressor::min_block_size_log2;

compressor::BlockEncoder make_encoder()
{
    return {[] {
        return std::make_unique<compressor::Engine>(block_size_log2);
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

std::string encode(const std::string& input)
{
    auto encoder = make_encoder();
    std::istringstream in(input);
    std::ostringstream out;
    if (!encoder.encode(in, out))
    {
        std::cerr << "encoding failed\n";
        std::exit(1);
    }
    return out.str();
}

std::string archive(const fs::path& scratch)
{
    fs::remove_all(scratch);
    fs::create_directories(scratch / "docs" / "deep");
    fs::create_directories(scratch / "empty");
    write(scratch / "a.txt", text(3000));
    write(scratch / "docs" / "b.bin", noise(5000));
    write(scratch / "docs" / "deep" / "c.txt", text(100));
    write(scratch / "docs" / "zero", "");
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
    if (!entries || !compressor::write_archive(encoder, scratch, *entries, out))
    {
        std::cerr << "archiving failed\n";
        std::exit(1);
    }
    std::error_code error;
    fs::remove_all(scratch, error);
    return out.str();
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
    const std::string plain = encode(text(10'000));
    const std::string stored = encode(noise(5000));
    const std::string empty = encode("");
    const std::string folder = archive(dir / ".scratch");

    write(dir / "container" / "text.ingot", plain);
    write(dir / "container" / "stored.ingot", stored);
    write(dir / "container" / "empty.ingot", empty);
    write(dir / "container" / "archive.ingot", folder);
    write(dir / "archive" / "archive.ingot", folder);
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
