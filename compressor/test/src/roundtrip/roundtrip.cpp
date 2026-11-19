#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/compressor.h>

namespace
{

namespace fs = std::filesystem;

constexpr const char* samples_dir = COMPRESSOR_SAMPLES_DIR;

// Lists with an error_code: this runs during static initialization, where a throw would abort.
std::vector<fs::path> sample_files()
{
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(samples_dir, error))
    {
        if (entry.is_regular_file(error))
        {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

std::vector<char> read_bytes(const fs::path& file)
{
    std::ifstream stream(file, std::ios::in | std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_bytes(const fs::path& file, const std::string& bytes)
{
    std::ofstream stream(file, std::ios::out | std::ios::trunc | std::ios::binary);
    stream << bytes;
}

bool encode(const fs::path& input, const fs::path& output)
{
    compressor::Compressor compressor;
    return compressor.encode(input, output);
}

bool decode(const fs::path& input, const fs::path& output)
{
    compressor::Compressor compressor;
    return compressor.decode(input, output);
}

fs::path fresh_work_dir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string test_name(const testing::TestParamInfo<fs::path>& info)
{
    std::string name = info.param.filename().string();
    std::ranges::replace_if(name, [](unsigned char c) {
        return std::isalnum(c) == 0;
    }, '_');
    return name;
}

} // namespace

class RoundTripTests : public testing::TestWithParam<fs::path>
{
protected:
    void SetUp() override
    {
        work_dir_ = fresh_work_dir("compressor_roundtrip_" + test_name({GetParam(), 0}));
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(work_dir_, error);
    }

    fs::path work_dir_;
};

TEST(RoundTripSamplesTests, Samples_directory_is_not_empty)
{
    EXPECT_FALSE(sample_files().empty()) << "no samples in " << samples_dir;
}

TEST_P(RoundTripTests, Encode_then_decode_restores_the_file)
{
    const fs::path& original = GetParam();
    const fs::path encoded = work_dir_ / "encoded.bin";
    const fs::path decoded = work_dir_ / "decoded.bin";
    ASSERT_TRUE(encode(original, encoded));
    ASSERT_TRUE(decode(encoded, decoded));

    const auto expected = read_bytes(original);
    const auto actual = read_bytes(decoded);
    ASSERT_FALSE(expected.empty());
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_TRUE(expected == actual);
}

INSTANTIATE_TEST_SUITE_P(Samples, RoundTripTests, testing::ValuesIn(sample_files()), test_name);

class EdgeCaseTests : public testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = testing::UnitTest::GetInstance()->current_test_info();
        work_dir_ = fresh_work_dir(std::string("compressor_edgecase_") + info->name());
        input_ = work_dir_ / "input.bin";
        encoded_ = work_dir_ / "encoded.bin";
        decoded_ = work_dir_ / "decoded.bin";
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(work_dir_, error);
    }

    void round_trip()
    {
        ASSERT_TRUE(encode(input_, encoded_));
        ASSERT_TRUE(decode(encoded_, decoded_));
    }

    // Encodes `text` and returns the encoded file's bytes.
    std::string encoded_bytes(const std::string& text)
    {
        write_bytes(input_, text);
        EXPECT_TRUE(encode(input_, encoded_));
        const auto bytes = read_bytes(encoded_);
        return {bytes.begin(), bytes.end()};
    }

    // Long enough that Huffman beats storing it.
    static std::string compressible()
    {
        std::string text;
        for (int i = 0; i < 50; ++i)
        {
            text += "abracadabra";
        }
        return text;
    }

    fs::path work_dir_;
    fs::path input_;
    fs::path encoded_;
    fs::path decoded_;
};

TEST_F(EdgeCaseTests, Empty_input_round_trips_to_an_empty_file)
{
    write_bytes(input_, "");
    round_trip();

    ASSERT_TRUE(fs::exists(decoded_));
    EXPECT_EQ(fs::file_size(decoded_), 0U);
}

TEST_F(EdgeCaseTests, Single_symbol_input_round_trips)
{
    write_bytes(input_, "aaaa");
    round_trip();

    const std::vector<char> expected{'a', 'a', 'a', 'a'};
    EXPECT_EQ(read_bytes(decoded_), expected);
}

TEST_F(EdgeCaseTests, Encoding_a_missing_input_leaves_the_output_untouched)
{
    write_bytes(encoded_, "keep");
    EXPECT_FALSE(encode(work_dir_ / "missing.bin", encoded_));

    const std::vector<char> expected{'k', 'e', 'e', 'p'};
    EXPECT_EQ(read_bytes(encoded_), expected);
}

TEST_F(EdgeCaseTests, Output_files_are_created_when_missing)
{
    write_bytes(input_, "abracadabra");
    ASSERT_TRUE(encode(input_, encoded_));
    ASSERT_TRUE(fs::exists(encoded_));
    ASSERT_TRUE(decode(encoded_, decoded_));

    const std::string text = "abracadabra";
    EXPECT_EQ(read_bytes(decoded_), std::vector<char>(text.begin(), text.end()));
}

TEST_F(EdgeCaseTests, Existing_longer_output_files_are_truncated)
{
    const std::string leftover(4096, 'x');
    write_bytes(input_, "abracadabra");
    write_bytes(encoded_, leftover);
    write_bytes(decoded_, leftover);
    ASSERT_TRUE(encode(input_, encoded_));
    ASSERT_TRUE(decode(encoded_, decoded_));

    const std::string text = "abracadabra";
    EXPECT_EQ(read_bytes(decoded_), std::vector<char>(text.begin(), text.end()));
}

TEST_F(EdgeCaseTests, Decoding_a_truncated_header_fails_and_creates_no_output)
{
    write_bytes(encoded_, encoded_bytes(compressible()).substr(0, 100));

    EXPECT_FALSE(decode(encoded_, decoded_));
    EXPECT_FALSE(fs::exists(decoded_));
}

TEST_F(EdgeCaseTests, Decoding_a_truncated_payload_fails_and_leaves_the_output_untouched)
{
    const std::string bytes = encoded_bytes(compressible());
    write_bytes(encoded_, bytes.substr(0, bytes.size() - 1));
    write_bytes(decoded_, "keep");

    EXPECT_FALSE(decode(encoded_, decoded_));
    const std::vector<char> expected{'k', 'e', 'e', 'p'};
    EXPECT_EQ(read_bytes(decoded_), expected);
}

TEST_F(EdgeCaseTests, Incompressible_input_is_stored_with_only_the_framing_added)
{
    const fs::path sample = fs::path(samples_dir) / "flower.png";
    ASSERT_TRUE(fs::exists(sample));
    ASSERT_TRUE(encode(sample, encoded_));

    // Frame header, one 13-byte header per 1 MiB block, end block.
    const std::uintmax_t blocks = (fs::file_size(sample) + (1U << 20U) - 1) >> 20U;
    EXPECT_EQ(fs::file_size(encoded_), fs::file_size(sample) + 8 + (13 * blocks) + 9);
    EXPECT_EQ(read_bytes(encoded_)[8], '\0'); // first block's method: stored
}

TEST_F(EdgeCaseTests, Decoding_a_changed_byte_fails_the_checksum_and_creates_no_output)
{
    const fs::path sample = fs::path(samples_dir) / "flower.png";
    ASSERT_TRUE(encode(sample, encoded_));
    std::vector<char> bytes = read_bytes(encoded_);
    bytes[100] = bytes[100] == 'A' ? 'B' : 'A';
    write_bytes(encoded_, std::string(bytes.begin(), bytes.end()));

    EXPECT_FALSE(decode(encoded_, decoded_));
    EXPECT_FALSE(fs::exists(decoded_));
}

TEST_F(EdgeCaseTests, Decoding_with_bytes_appended_fails)
{
    write_bytes(encoded_, encoded_bytes(compressible()) + "x");

    EXPECT_FALSE(decode(encoded_, decoded_));
    EXPECT_FALSE(fs::exists(decoded_));
}
