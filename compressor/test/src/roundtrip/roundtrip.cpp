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
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<char> read_bytes(const fs::path& file)
{
    std::ifstream stream(file, std::ios::in | std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// DataWriter::open can't create a file yet (Phase 2), so the outputs are created up front.
void touch(const fs::path& file)
{
    const std::ofstream stream(file, std::ios::out | std::ios::trunc | std::ios::binary);
}

std::string test_name(const testing::TestParamInfo<fs::path>& info)
{
    std::string name = info.param.filename().string();
    std::replace_if(name.begin(), name.end(), [](unsigned char c) {
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
        work_dir_ =
            fs::temp_directory_path() / ("compressor_roundtrip_" + test_name({GetParam(), 0}));
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
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
    touch(encoded);
    touch(decoded);

    compressor::Compressor compressor;
    compressor::EncoderTask encoder(original.string(), encoded.string());
    compressor.encode(encoder);
    compressor::DecoderTask decoder(encoded.string(), decoded.string());
    compressor.decode(decoder);

    const auto expected = read_bytes(original);
    const auto actual = read_bytes(decoded);
    ASSERT_FALSE(expected.empty());
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_TRUE(expected == actual);
}

INSTANTIATE_TEST_SUITE_P(Samples, RoundTripTests, testing::ValuesIn(sample_files()), test_name);
