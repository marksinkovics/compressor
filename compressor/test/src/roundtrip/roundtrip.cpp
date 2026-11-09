#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/compressor.h>
#include <compressor/task/decoder.h>
#include <compressor/task/encoder.h>

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

void encode(const fs::path& input, const fs::path& output)
{
    compressor::Compressor compressor;
    compressor::EncoderTask task(input.string(), output.string());
    compressor.encode(task);
}

void decode(const fs::path& input, const fs::path& output)
{
    compressor::Compressor compressor;
    compressor::DecoderTask task(input.string(), output.string());
    compressor.decode(task);
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
    encode(original, encoded);
    decode(encoded, decoded);

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
        encode(input_, encoded_);
        decode(encoded_, decoded_);
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
    encode(work_dir_ / "missing.bin", encoded_);

    const std::vector<char> expected{'k', 'e', 'e', 'p'};
    EXPECT_EQ(read_bytes(encoded_), expected);
}

TEST_F(EdgeCaseTests, Output_files_are_created_when_missing)
{
    write_bytes(input_, "abracadabra");
    encode(input_, encoded_);
    ASSERT_TRUE(fs::exists(encoded_));
    decode(encoded_, decoded_);

    const std::string text = "abracadabra";
    EXPECT_EQ(read_bytes(decoded_), std::vector<char>(text.begin(), text.end()));
}

TEST_F(EdgeCaseTests, Existing_longer_output_files_are_truncated)
{
    const std::string leftover(4096, 'x');
    write_bytes(input_, "abracadabra");
    write_bytes(encoded_, leftover);
    write_bytes(decoded_, leftover);
    encode(input_, encoded_);
    decode(encoded_, decoded_);

    const std::string text = "abracadabra";
    EXPECT_EQ(read_bytes(decoded_), std::vector<char>(text.begin(), text.end()));
}
