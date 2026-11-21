#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/compressor.h>
#include <compressor/data/data.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/range/range.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace
{

namespace fs = std::filesystem;

constexpr uint8_t small_blocks = 12; // 4 KiB, so a few KB make many blocks

std::string text(std::size_t size)
{
    std::string out(size, '\0');
    uint32_t state = 17;
    for (char& c : out)
    {
        state = (state * 1103515245U) + 12345U;
        c = "range reads decode only their blocks\n"[(state >> 16U) % 37];
    }
    return out;
}

std::string encode(const std::string& input, bool single_block = false)
{
    compressor::BlockEncoder encoder([] {
        return std::make_unique<compressor::Engine>(small_blocks, 2);
    }, small_blocks, 2, single_block);
    std::istringstream in(input);
    std::ostringstream out;
    EXPECT_TRUE(encoder.encode(in, out));
    return out.str();
}

// An engine that counts the blocks it decodes.
class CountingEngine : public compressor::IEngine
{
public:
    explicit CountingEngine(std::atomic<int>& decoded)
        : decoded_(decoded)
    {
    }

    void print_graph() const override {}

    void print_dict() const override {}

    compressor::EncodedBlock encode_block(std::span<const uint8_t> bytes) override
    {
        return engine_.encode_block(bytes);
    }

    std::expected<void, compressor::Error> decode_block(const compressor::EncodedBlock& block,
                                                        std::vector<uint8_t>& out) override
    {
        ++decoded_;
        return engine_.decode_block(block, out);
    }

private:
    std::atomic<int>& decoded_;
    compressor::Engine engine_{small_blocks, 1};
};

std::expected<std::string, compressor::Error>
read(const std::string& container, compressor::ByteRange range, int* blocks = nullptr)
{
    std::atomic<int> decoded{0};
    compressor::BlockDecoder decoder([&decoded] {
        return std::make_unique<CountingEngine>(decoded);
    }, 2);
    std::istringstream in(container);
    std::ostringstream out;
    if (auto result = compressor::decode_range(decoder, in, range, out); !result)
    {
        return std::unexpected(result.error());
    }
    if (blocks != nullptr)
    {
        *blocks = decoded;
    }
    return out.str();
}

} // namespace

TEST(RangeTests, Ranges_parse_as_from_colon_to)
{
    EXPECT_EQ(compressor::parse_range("10:20"), (compressor::ByteRange{.from = 10, .to = 20}));
    EXPECT_EQ(compressor::parse_range("10:"), (compressor::ByteRange{.from = 10, .to = {}}));
    EXPECT_EQ(compressor::parse_range(":20"), (compressor::ByteRange{.from = 0, .to = 20}));
    EXPECT_EQ(compressor::parse_range(":"), (compressor::ByteRange{}));
    EXPECT_EQ(compressor::parse_range("7:7"), (compressor::ByteRange{.from = 7, .to = 7}));
    for (const char* bad : {"", "10", "20:10", "a:b", "1:2:3", "-1:5", " 1:2", "1k:2"})
    {
        EXPECT_FALSE(compressor::parse_range(bad)) << bad;
    }
}

TEST(RangeTests, A_slice_passes_on_only_its_bytes)
{
    std::ostringstream out;
    compressor::SliceBuf slice(out.rdbuf(), 3, 4);
    std::ostream sliced(&slice);
    sliced << "ab" << "cdefg" << 'h' << "ijk";
    EXPECT_EQ(out.str(), "defg");
}

TEST(RangeTests, A_range_decodes_only_the_blocks_that_hold_it)
{
    const std::string input = text(10 * 4096 + 100);
    const std::string container = encode(input);
    int blocks = 0;
    EXPECT_EQ(read(container, {.from = 4096 * 3 + 5, .to = 4096 * 3 + 50}, &blocks),
              input.substr((4096 * 3) + 5, 45));
    EXPECT_EQ(blocks, 1);
    EXPECT_EQ(read(container, {.from = 4090, .to = 8200}, &blocks),
              input.substr(4090, 8200 - 4090));
    EXPECT_EQ(blocks, 3);
    EXPECT_EQ(read(container, {.from = input.size() - 10, .to = {}}, &blocks),
              input.substr(input.size() - 10));
    EXPECT_EQ(blocks, 1);
    EXPECT_EQ(read(container, {.from = 0, .to = input.size() + 999}), input);
    EXPECT_EQ(read(container, {.from = input.size(), .to = {}}), "");
    EXPECT_EQ(read(container, {.from = input.size() + 1, .to = {}}),
              std::unexpected(compressor::Error::bad_range));
}

TEST(RangeTests, Single_block_frames_and_empty_files_read_too)
{
    const std::string input = text(3000);
    EXPECT_EQ(read(encode(input, true), {.from = 100, .to = 200}), input.substr(100, 100));
    EXPECT_EQ(read(encode(""), {.from = 0, .to = {}}), "");
    EXPECT_EQ(read(encode(""), {.from = 1, .to = {}}),
              std::unexpected(compressor::Error::bad_range));
}

TEST(RangeTests, The_block_table_comes_from_the_headers)
{
    const std::string input = text(3 * 4096 + 7);
    const std::string container = encode(input);
    std::istringstream in(container);
    const auto table = compressor::read_block_table(in);
    ASSERT_TRUE(table);
    EXPECT_EQ(table->block_size_log2, small_blocks);
    EXPECT_EQ(table->total_size, input.size());
    ASSERT_EQ(table->offsets.size(), 5U); // 4 blocks, then the end block
    EXPECT_EQ(table->offsets.front(), compressor::frame_header_bytes);
    EXPECT_EQ(table->offsets.back(), container.size() - compressor::end_block_bytes);

    // A total that doesn't fit the block count, and a cut file.
    std::string wrong_total = container;
    wrong_total[wrong_total.size() - 8] =
        static_cast<char>(wrong_total[wrong_total.size() - 8] + 1);
    wrong_total[wrong_total.size() - 7] = 0x40;
    std::istringstream bad(wrong_total);
    EXPECT_EQ(compressor::read_block_table(bad),
              std::unexpected(compressor::Error::corrupt_header));
    std::istringstream cut(container.substr(0, container.size() - 4));
    EXPECT_FALSE(compressor::read_block_table(cut));
}

TEST(RangeTests, A_damaged_block_in_the_range_fails_and_one_outside_does_not_matter)
{
    const std::string input = text(std::size_t{4} * 4096);
    std::string container = encode(input);
    std::istringstream in(container);
    const auto table = compressor::read_block_table(in);
    ASSERT_TRUE(table);
    // Damage the last byte of block 2's payload.
    const std::size_t last = table->offsets[3] - 1;
    container[last] = static_cast<char>(static_cast<unsigned char>(container[last]) ^ 0x5AU);
    EXPECT_FALSE(read(container, {.from = (2 * 4096) + 1, .to = (2 * 4096) + 2}));
    EXPECT_EQ(read(container, {.from = 4096, .to = 4096 + 10}), input.substr(4096, 10));
}

TEST(RangeTests, A_file_in_an_archive_reads_by_range)
{
    const fs::path dir = fs::temp_directory_path() / "compressor_range_archive";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir / "tree");
    const std::string big = text(3'000'000);
    std::ofstream(dir / "tree" / "a.txt", std::ios::binary) << "first";
    std::ofstream(dir / "tree" / "big.txt", std::ios::binary) << big;
    compressor::Compressor compressor(compressor::CompressorSettings{.threads = 2});
    ASSERT_TRUE(compressor.encode(dir / "tree", dir / "tree.ingot"));
    ASSERT_TRUE(compressor.read_range(dir / "tree.ingot", "big.txt",
                                      {.from = 1'048'000, .to = 2'100'000}, dir / "part"));
    std::ifstream part(dir / "part", std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(part), {}),
              big.substr(1'048'000, 2'100'000 - 1'048'000));
    EXPECT_FALSE(compressor.read_range(dir / "tree.ingot", "missing", {}, dir / "x"));
    EXPECT_FALSE(compressor.read_range(dir / "tree.ingot", "", {}, dir / "x"));
    EXPECT_FALSE(compressor.read_range(dir / "tree.ingot", "big.txt",
                                       {.from = big.size() + 1, .to = {}}, dir / "x"));
    fs::remove_all(dir, error);
}
