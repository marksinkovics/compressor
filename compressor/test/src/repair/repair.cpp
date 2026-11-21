#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <compressor/compressor.h>
#include <compressor/error.h>
#include <compressor/repair/repair.h>

namespace
{

std::string noise(std::size_t size, uint32_t seed)
{
    std::string bytes(size, '\0');
    for (char& c : bytes)
    {
        seed = (seed * 1103515245U) + 12345U;
        c = static_cast<char>(seed >> 16U);
    }
    return bytes;
}

std::string protect(const std::string& file, unsigned percent = compressor::default_repair_percent)
{
    std::istringstream in(file);
    std::stringstream out;
    EXPECT_TRUE(compressor::write_repair(in, file.size(), percent, out));
    return out.str();
}

std::expected<std::string, compressor::Error> fix(const std::string& damaged,
                                                  const std::string& repair)
{
    std::istringstream file(damaged);
    std::istringstream sidecar(repair);
    std::stringstream out;
    if (auto result = compressor::repair_file(file, sidecar, out); !result)
    {
        return std::unexpected(result.error());
    }
    return out.str();
}

void damage_shard(std::string& file, const compressor::RepairLayout& layout, unsigned shard)
{
    const std::size_t at = (shard * layout.shard_size) + (layout.shard_size / 3);
    file[at] = static_cast<char>(static_cast<unsigned char>(file[at]) ^ 0x40U);
}

} // namespace

TEST(RepairTests, The_layout_takes_shards_of_64_KiB_up_to_200_and_parity_by_percent)
{
    const auto small = compressor::repair_layout(100, 5);
    EXPECT_EQ(small.data_shards, 1U);
    EXPECT_EQ(small.parity_shards, 1U);
    EXPECT_EQ(small.shard_size, 100U);
    const auto mid = compressor::repair_layout(std::size_t{1300} * 1000, 5);
    EXPECT_EQ(mid.data_shards, 20U);
    EXPECT_EQ(mid.parity_shards, 1U);
    const auto big = compressor::repair_layout(uint64_t{10} << 30U, 10);
    EXPECT_EQ(big.data_shards, 200U);
    EXPECT_EQ(big.parity_shards, 20U);
    EXPECT_GE(big.shard_size * big.data_shards, uint64_t{10} << 30U);
}

TEST(RepairTests, Damaged_shards_up_to_the_parity_count_are_rebuilt)
{
    const std::string original = noise(std::size_t{1300} * 1000 + 17, 1);
    const std::string repair = protect(original, 10);
    const auto layout = compressor::repair_layout(original.size(), 10);
    ASSERT_EQ(layout.parity_shards, 2U);

    std::istringstream clean(original);
    std::istringstream sidecar(repair);
    const auto check = compressor::check_repair(clean, sidecar);
    ASSERT_TRUE(check);
    EXPECT_TRUE(check->intact());

    for (const unsigned a : {0U, 7U, layout.data_shards - 1})
    {
        for (const unsigned b : {3U, 12U})
        {
            std::string damaged = original;
            damage_shard(damaged, layout, a);
            damage_shard(damaged, layout, b);
            EXPECT_EQ(fix(damaged, repair), original) << a << " " << b;
        }
    }
    // One shard more than the parity: found, but not recoverable.
    std::string too_much = original;
    for (const unsigned s : {1U, 2U, 3U})
    {
        damage_shard(too_much, layout, s);
    }
    std::istringstream file(too_much);
    std::istringstream again(repair);
    const auto bad = compressor::check_repair(file, again);
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->damaged_data, (std::vector<unsigned>{1, 2, 3}));
    EXPECT_FALSE(bad->recoverable());
    EXPECT_EQ(fix(too_much, repair), std::unexpected(compressor::Error::corrupt_payload));
}

TEST(RepairTests, Damaged_parity_and_a_cut_file_count_as_damaged_shards)
{
    const std::string original = noise(std::size_t{700} * 1000, 2);
    std::string repair = protect(original, 20);
    const auto layout = compressor::repair_layout(original.size(), 20);
    ASSERT_GE(layout.parity_shards, 2U);
    // The first parity shard damaged, and the file cut inside its last shard.
    const std::size_t parity = repair.size() - (layout.parity_shards * layout.shard_size) + 5;
    repair[parity] = static_cast<char>(static_cast<unsigned char>(repair[parity]) ^ 0x01U);
    const std::string cut = original.substr(0, original.size() - 1000);
    EXPECT_EQ(fix(cut, repair), original);
    std::istringstream file(cut);
    std::istringstream sidecar(repair);
    const auto check = compressor::check_repair(file, sidecar);
    ASSERT_TRUE(check);
    EXPECT_FALSE(check->size_matches);
    EXPECT_EQ(check->damaged_parity, (std::vector<unsigned>{0}));
    EXPECT_EQ(check->damaged_data, (std::vector<unsigned>{layout.data_shards - 1}));
}

TEST(RepairTests, Small_files_and_a_damaged_header)
{
    for (const std::size_t size :
         {std::size_t{1}, std::size_t{100}, std::size_t{65536}, std::size_t{65537}})
    {
        const std::string original = noise(size, 3);
        const std::string repair = protect(original);
        std::string damaged = original;
        damaged[size / 2] =
            static_cast<char>(static_cast<unsigned char>(damaged[size / 2]) ^ 0x10U);
        EXPECT_EQ(fix(damaged, repair), original) << size;
    }
    const std::string original = noise(5000, 4);
    std::string repair = protect(original);
    repair[10] = static_cast<char>(static_cast<unsigned char>(repair[10]) ^ 0x01U); // the header
    EXPECT_EQ(fix(original, repair), std::unexpected(compressor::Error::corrupt_header));
    EXPECT_EQ(fix(original, "not a repair file"),
              std::unexpected(compressor::Error::corrupt_header));
}

TEST(RepairTests, Encoding_with_repair_data_and_repairing_files)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "compressor_repair_files";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir / "tree");
    const std::string original = noise(std::size_t{900} * 1000, 6);
    std::ofstream(dir / "input.bin", std::ios::binary) << original;
    std::ofstream(dir / "tree" / "file.bin", std::ios::binary) << original.substr(0, 300'000);

    compressor::CompressorSettings settings{.threads = 2};
    settings.repair_percent = 10;
    compressor::Compressor compressor(settings);
    ASSERT_TRUE(compressor.encode(dir / "input.bin", dir / "input.ingot"));
    ASSERT_TRUE(fs::exists(dir / "input.ingot.repair"));
    ASSERT_TRUE(compressor.encode(dir / "tree", dir / "tree.ingot"));
    ASSERT_TRUE(fs::exists(dir / "tree.ingot.repair"));
    ASSERT_TRUE(
        compressor.encode_as(dir / "input.bin", dir / "input.gz", compressor::OutputFormat::gzip));
    ASSERT_TRUE(fs::exists(dir / "input.gz.repair"));
    EXPECT_FALSE(compressor.encode(dir / "input.bin", "-")); // no file for the repair data

    // Damage the encoded file in two places, then repair it into another file and in place.
    std::string encoded;
    {
        std::ifstream in(dir / "input.ingot", std::ios::binary);
        encoded.assign(std::istreambuf_iterator<char>(in), {});
    }
    std::string damaged = encoded;
    damaged[100] = static_cast<char>(static_cast<unsigned char>(damaged[100]) ^ 0x01U);
    damaged[damaged.size() - 100] =
        static_cast<char>(static_cast<unsigned char>(damaged[damaged.size() - 100]) ^ 0x01U);
    std::ofstream(dir / "input.ingot", std::ios::binary | std::ios::trunc) << damaged;
    EXPECT_FALSE(compressor.verify(dir / "input.ingot"));
    ASSERT_TRUE(compressor.repair(dir / "input.ingot", {}, dir / "fixed.ingot"));
    ASSERT_TRUE(compressor.repair(dir / "input.ingot", {}, {}));
    for (const char* name : {"input.ingot", "fixed.ingot"})
    {
        std::ifstream in(dir / name, std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), {}), encoded) << name;
    }
    EXPECT_TRUE(compressor.verify(dir / "input.ingot"));
    EXPECT_TRUE(compressor.repair(dir / "input.ingot", {}, {})); // intact

    // Too much damage is refused and leaves the file as it was.
    std::string ruined = encoded;
    for (std::size_t at = 50; at < ruined.size(); at += 65536)
    {
        ruined[at] = static_cast<char>(static_cast<unsigned char>(ruined[at]) ^ 0x01U);
    }
    std::ofstream(dir / "input.ingot", std::ios::binary | std::ios::trunc) << ruined;
    EXPECT_FALSE(compressor.repair(dir / "input.ingot", {}, {}));
    std::ifstream still(dir / "input.ingot", std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(still), {}), ruined);
    fs::remove_all(dir, error);
}
