#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include <compressor/checksum/crc32.h>
#include <compressor/compressor.h>
#include <compressor/data/data.h>
#include <compressor/deflate/deflate.h>
#include <compressor/deflate/inflate.h>
#include <compressor/dictionary/dictionary.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/lz77/lz77.h>
#include <compressor/lzr/lzr.h>

namespace
{

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

Bytes bytes(const std::string& text)
{
    return {text.begin(), text.end()};
}

// A small JSON-like record: the same keys and structure every time, different values.
std::string record(uint32_t seed)
{
    std::string name;
    for (int i = 0; i < 8; ++i)
    {
        seed = (seed * 1103515245U) + 12345U;
        name.push_back(static_cast<char>('a' + ((seed >> 16U) % 26)));
    }
    return R"({"name": ")" + name + R"(", "version-string": ")" + std::to_string(seed % 97) + "." +
           std::to_string(seed % 13) +
           R"(", "description": "A library for reading and writing records.", )"
           R"("homepage": "https://github.com/example/)" +
           name +
           R"(", "license": "MIT", "dependencies": [{"name": "vcpkg-cmake", "host": )"
           R"(true}, {"name": "vcpkg-cmake-config", "host": true}]})"
           "\n";
}

std::vector<Bytes> records(std::size_t count, uint32_t first)
{
    std::vector<Bytes> samples;
    samples.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        samples.push_back(bytes(record(first + static_cast<uint32_t>(i))));
    }
    return samples;
}

compressor::Dictionary trained()
{
    return compressor::make_dictionary(compressor::train_dictionary(records(400, 1), 4096));
}

std::string inflate_with_zlib(const Bytes& stream, const Bytes& dictionary, std::size_t size)
{
    std::string out(size, '\0');
    z_stream z{};
    EXPECT_EQ(inflateInit2(&z, -15), Z_OK);
    EXPECT_EQ(inflateSetDictionary(&z, dictionary.data(), static_cast<uInt>(dictionary.size())),
              Z_OK);
    z.next_in = const_cast<Bytef*>(stream.data());
    z.avail_in = static_cast<uInt>(stream.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(inflate(&z, Z_FINISH), Z_STREAM_END);
    inflateEnd(&z);
    return out;
}

} // namespace

TEST(DictionaryTests, Training_keeps_what_the_samples_share)
{
    const Bytes dictionary = compressor::train_dictionary(records(400, 1), 4096);
    ASSERT_LE(dictionary.size(), 4096U);
    ASSERT_GE(dictionary.size(), 1024U);
    const std::string text(dictionary.begin(), dictionary.end());
    EXPECT_NE(text.find("\"vcpkg-cmake-config\", \"host\": true}"), std::string::npos);
    EXPECT_NE(text.find("A library for reading and writing records."), std::string::npos);
}

TEST(DictionaryTests, Small_samples_are_the_dictionary_and_unshared_ones_give_none)
{
    const std::vector<Bytes> small{bytes("abc"), bytes("defg")};
    EXPECT_EQ(compressor::train_dictionary(small, 100), bytes("abcdefg"));

    std::vector<Bytes> noise(20, Bytes(4000));
    uint32_t state = 1;
    for (Bytes& sample : noise)
    {
        for (uint8_t& b : sample)
        {
            state = (state * 1103515245U) + 12345U;
            b = static_cast<uint8_t>(state >> 16U);
        }
    }
    EXPECT_TRUE(compressor::train_dictionary(noise, 4096).empty());
}

TEST(DictionaryTests, A_dictionary_file_is_its_bytes_with_their_crc_as_id)
{
    const fs::path dir = fs::temp_directory_path() / "compressor_dictionary_file";
    fs::create_directories(dir);
    std::ofstream(dir / "dict", std::ios::binary) << "shared content";
    const auto dictionary = compressor::read_dictionary(dir / "dict");
    ASSERT_TRUE(dictionary);
    EXPECT_EQ(dictionary->bytes, bytes("shared content"));
    EXPECT_EQ(dictionary->id, compressor::crc32(bytes("shared content")));

    std::ofstream(dir / "empty", std::ios::binary).close();
    EXPECT_EQ(compressor::read_dictionary(dir / "empty").error(),
              compressor::Error::corrupt_header);
    std::ofstream(dir / "big", std::ios::binary)
        << std::string(compressor::max_dictionary_bytes + 1, 'x');
    EXPECT_EQ(compressor::read_dictionary(dir / "big").error(), compressor::Error::corrupt_header);
    EXPECT_EQ(compressor::read_dictionary(dir / "missing").error(), compressor::Error::read_failed);
    std::error_code error;
    fs::remove_all(dir, error);
}

TEST(DictionaryTests, Deflate_matches_into_the_dictionary_as_zlib_reads_it)
{
    const compressor::Dictionary dictionary = trained();
    const Bytes input = bytes(record(5000));
    const Bytes plain = compressor::deflate_bytes(input);
    const Bytes primed = compressor::deflate_bytes(input, {}, dictionary.bytes);
    EXPECT_LT(primed.size() * 2, plain.size());
    EXPECT_EQ(inflate_with_zlib(primed, dictionary.bytes, input.size()),
              std::string(input.begin(), input.end()));

    Bytes out{9};
    ASSERT_TRUE(compressor::inflate_bytes(primed, out, input.size(), dictionary.bytes));
    EXPECT_EQ(Bytes(out.begin() + 1, out.end()), input);
    Bytes without;
    EXPECT_FALSE(compressor::inflate_bytes(primed, without, input.size()));
}

TEST(DictionaryTests, Lzr_matches_into_the_dictionary)
{
    const compressor::Dictionary dictionary = trained();
    const Bytes input = bytes(record(6000) + record(6001));
    const auto options = compressor::lz77_options_for_level(7);
    const Bytes plain = compressor::lzr_encode(input, options);
    const Bytes primed = compressor::lzr_encode(input, options, dictionary.bytes);
    EXPECT_LT(primed.size() * 2, plain.size());
    Bytes out{9};
    ASSERT_TRUE(compressor::lzr_decode(primed, out, input.size(), dictionary.bytes));
    EXPECT_EQ(Bytes(out.begin() + 1, out.end()), input);
    Bytes without;
    EXPECT_FALSE(compressor::lzr_decode(primed, without, input.size()));
    // Without a dictionary the stream is what it always was.
    Bytes back;
    ASSERT_TRUE(compressor::lzr_decode(plain, back, input.size()));
    EXPECT_EQ(back, input);
}

TEST(DictionaryTests, The_engine_names_the_dictionary_and_needs_the_same_one)
{
    auto dictionary = std::make_shared<const compressor::Dictionary>(trained());
    const Bytes input = bytes(record(7000));
    for (const unsigned level : {6U, compressor::bwt_level})
    {
        compressor::Engine encoder(compressor::default_block_size_log2, level, dictionary);
        const compressor::EncodedBlock block = encoder.encode_block(input);
        ASSERT_EQ(block.method_, compressor::Method::pipeline) << level;
        const uint8_t coder = block.payload_[(block.payload_[0] * 5) + 1];
        EXPECT_TRUE(coder == static_cast<uint8_t>(compressor::Coder::lz77_huffman_dictionary) ||
                    coder == static_cast<uint8_t>(compressor::Coder::lzr_dictionary))
            << level;
        compressor::Engine plain(compressor::default_block_size_log2, level);
        EXPECT_LT(block.payload_.size() * 2, plain.encode_block(input).payload_.size()) << level;

        compressor::Engine same(compressor::default_block_size_log2, 1, dictionary);
        Bytes out;
        ASSERT_TRUE(same.decode_block(block, out)) << level;
        EXPECT_EQ(out, input);
        compressor::Engine none;
        EXPECT_EQ(none.decode_block(block, out).error(), compressor::Error::dictionary_missing);
        compressor::Engine other(
            compressor::default_block_size_log2, 1,
            std::make_shared<const compressor::Dictionary>(compressor::make_dictionary(
                Bytes(dictionary->bytes.begin() + 1, dictionary->bytes.end()))));
        EXPECT_EQ(other.decode_block(block, out).error(), compressor::Error::dictionary_mismatch);
    }
}

TEST(DictionaryTests, Training_encoding_and_decoding_files)
{
    const fs::path dir = fs::temp_directory_path() / "compressor_dictionary_files";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir / "samples");
    for (int i = 0; i < 300; ++i)
    {
        std::ofstream(dir / "samples" / (std::to_string(i) + ".json"), std::ios::binary)
            << record(static_cast<uint32_t>(i));
    }
    std::ofstream(dir / "input.json", std::ios::binary) << record(9999);

    compressor::Compressor trainer(compressor::CompressorSettings{.threads = 1});
    ASSERT_TRUE(trainer.train({dir / "samples"}, dir / "dict", 2048));
    auto dictionary = compressor::read_dictionary(dir / "dict");
    ASSERT_TRUE(dictionary);
    EXPECT_LE(dictionary->bytes.size(), 2048U);

    compressor::CompressorSettings settings{.threads = 1};
    settings.dictionary = std::make_shared<const compressor::Dictionary>(*dictionary);
    compressor::Compressor with(settings);
    ASSERT_TRUE(with.encode(dir / "input.json", dir / "input.ingot"));
    compressor::Compressor without(compressor::CompressorSettings{.threads = 1});
    ASSERT_TRUE(without.encode(dir / "input.json", dir / "plain.ingot"));
    EXPECT_LT(fs::file_size(dir / "input.ingot") * 2, fs::file_size(dir / "plain.ingot"));

    EXPECT_FALSE(without.decode(dir / "input.ingot", dir / "missing.json"));
    EXPECT_FALSE(fs::exists(dir / "missing.json"));
    ASSERT_TRUE(with.decode(dir / "input.ingot", dir / "back.json"));
    std::ifstream back(dir / "back.json", std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(back), {}), record(9999));
    EXPECT_FALSE(
        with.encode_as(dir / "input.json", dir / "input.gz", compressor::OutputFormat::gzip));
    EXPECT_FALSE(trainer.train({dir / "nothing"}, dir / "none", 2048));
    fs::remove_all(dir, error);
}
