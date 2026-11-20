#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include <compressor/archive/archive.h>
#include <compressor/archive/metadata.h>
#include <compressor/deflate/zip.h>

namespace
{

namespace fs = std::filesystem;

uint64_t le(std::string_view bytes, std::size_t at, std::size_t size)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[at + i])) << (8 * i);
    }
    return value;
}

std::string inflate_raw(std::string_view compressed, std::size_t size)
{
    std::string out(size, '\0');
    z_stream stream{};
    EXPECT_EQ(inflateInit2(&stream, -15), Z_OK);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(inflate(&stream, Z_FINISH), Z_STREAM_END);
    EXPECT_EQ(stream.avail_in, 0U);
    inflateEnd(&stream);
    return out;
}

struct ZipEntry
{
    bool zip64 = false; // in its central record
    uint16_t flags = 0;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t external = 0;
    std::string contents;
};

// Reads an archive the way unzip does: end record, central directory, then each local header.
std::map<std::string, ZipEntry> read_zip(const std::string& zip)
{
    std::map<std::string, ZipEntry> entries;
    const std::size_t end = zip.size() - 22;
    EXPECT_EQ(le(zip, end, 4), 0x06054B50U);
    uint64_t count = le(zip, end + 10, 2);
    uint64_t at = le(zip, end + 16, 4);
    uint64_t central_size = le(zip, end + 12, 4);
    std::size_t central_end = end;
    if (count == 0xFFFF || at == 0xFFFFFFFF)
    {
        // Zip64: the locator just before the end record points at the Zip64 end record.
        const std::size_t locator = end - 20;
        EXPECT_EQ(le(zip, locator, 4), 0x07064B50U);
        const std::size_t record = le(zip, locator + 8, 8);
        EXPECT_EQ(le(zip, record, 4), 0x06064B50U);
        count = le(zip, record + 32, 8);
        central_size = le(zip, record + 40, 8);
        at = le(zip, record + 48, 8);
        central_end = record;
    }
    EXPECT_EQ(at + central_size, central_end);
    for (uint32_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(le(zip, at, 4), 0x02014B50U);
        ZipEntry entry;
        entry.flags = static_cast<uint16_t>(le(zip, at + 8, 2));
        entry.method = static_cast<uint16_t>(le(zip, at + 10, 2));
        entry.crc = static_cast<uint32_t>(le(zip, at + 16, 4));
        uint64_t compressed = le(zip, at + 20, 4);
        uint64_t size = le(zip, at + 24, 4);
        const uint64_t name_size = le(zip, at + 28, 2);
        const uint64_t extra_size = le(zip, at + 30, 2);
        entry.external = static_cast<uint32_t>(le(zip, at + 38, 4));
        uint64_t local = le(zip, at + 42, 4);
        const std::string name = zip.substr(at + 46, name_size);
        // The Zip64 extra field holds, in order, whichever of these said 0xFFFFFFFF.
        std::size_t field = at + 46 + name_size;
        if (extra_size > 0)
        {
            EXPECT_EQ(le(zip, field, 2), 1U) << name;
            field += 4;
            entry.zip64 = true;
            for (uint64_t* value : {&size, &compressed, &local})
            {
                if (*value == 0xFFFFFFFF)
                {
                    *value = le(zip, field, 8);
                    field += 8;
                }
            }
        }
        at += 46 + name_size + extra_size;

        EXPECT_EQ(le(zip, local, 4), 0x04034B50U) << name;
        EXPECT_EQ(zip.substr(local + 30, le(zip, local + 26, 2)), name);
        const std::size_t data = local + 30 + le(zip, local + 26, 2) + le(zip, local + 28, 2);
        const std::string_view stored = std::string_view(zip).substr(data, compressed);
        entry.contents = entry.method == 8 ? inflate_raw(stored, size) : std::string(stored);
        EXPECT_EQ(
            static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(entry.contents.data()),
                                        static_cast<uInt>(entry.contents.size()))),
            entry.crc)
            << name;
        if ((entry.flags & 0x0008U) != 0)
        {
            EXPECT_EQ(le(zip, data + compressed, 4), 0x08074B50U) << name;
            EXPECT_EQ(le(zip, data + compressed + 4, 4), entry.crc) << name;
            // A Zip64 local header (extra field 1) means 8-byte sizes in the descriptor.
            const bool local_zip64 =
                le(zip, local + 28, 2) != 0 && le(zip, local + 30 + le(zip, local + 26, 2), 2) == 1;
            EXPECT_EQ(le(zip, data + compressed + 8, local_zip64 ? 8 : 4), compressed) << name;
            EXPECT_EQ(le(zip, data + compressed + (local_zip64 ? 16 : 12), local_zip64 ? 8 : 4),
                      size)
                << name;
        }
        entries[name] = entry;
    }
    return entries;
}

class ZipTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        root_ = fs::temp_directory_path() / (std::string("compressor_zip_") + info->name());
        fs::remove_all(root_);
        fs::create_directories(root_ / "docs" / "empty");
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    std::string zip(bool force_zip64 = false) const
    {
        std::ostringstream warnings;
        const auto entries = compressor::scan_tree(root_, {}, warnings);
        EXPECT_TRUE(entries);
        std::ostringstream out;
        EXPECT_TRUE(compressor::write_zip(root_, *entries, out, {}, force_zip64));
        return out.str();
    }

    fs::path root_;
};

} // namespace

TEST_F(ZipTests, Files_directories_and_symlinks_read_back_with_their_modes)
{
    std::string big;
    for (int i = 0; i < 20'000; ++i)
    {
        big += "line " + std::to_string(i % 97) + "\n";
    }
    std::ofstream(root_ / "a.txt") << "hello zip\n";
    std::ofstream(root_ / "docs" / "big.txt") << big;
    std::ofstream(root_ / "docs" / "zero") << "";
    fs::create_symlink("a.txt", root_ / "link");
    ASSERT_FALSE(compressor::set_mode(root_ / "a.txt", 0640));

    const std::string archive = zip();
    EXPECT_LT(archive.size(), big.size() / 4);
    const auto entries = read_zip(archive);
    ASSERT_EQ(entries.size(), 6U);

    EXPECT_EQ(entries.at("a.txt").contents, "hello zip\n");
    EXPECT_EQ(entries.at("a.txt").method, 8);
    EXPECT_EQ(entries.at("a.txt").external >> 16U, 0100640U);
    EXPECT_EQ(entries.at("docs/big.txt").contents, big);
    EXPECT_EQ(entries.at("docs/zero").contents, "");
    EXPECT_EQ(entries.at("docs/").method, 0);
    EXPECT_EQ(entries.at("docs/").external >> 16U & 0170000U, 0040000U);
    EXPECT_EQ(entries.at("docs/").external & 0x10U, 0x10U);
    EXPECT_TRUE(entries.contains("docs/empty/"));
    EXPECT_EQ(entries.at("link").contents, "a.txt");
    EXPECT_EQ(entries.at("link").external >> 16U & 0170000U, 0120000U);
    for (const auto& [name, entry] : entries)
    {
        EXPECT_NE(entry.flags & 0x0800U, 0U) << name << " is not marked UTF-8";
    }
}

TEST_F(ZipTests, An_empty_folder_is_an_empty_archive)
{
    fs::remove_all(root_ / "docs");
    const std::string archive = zip();
    EXPECT_EQ(archive.size(), 22U);
    EXPECT_TRUE(read_zip(archive).empty());
}

TEST_F(ZipTests, Forced_zip64_records_read_back_the_same)
{
    std::ofstream(root_ / "a.txt") << "zip64 everywhere\n";
    std::ofstream(root_ / "docs" / "b.txt") << std::string(100'000, 'b');
    fs::create_symlink("a.txt", root_ / "link");

    const auto plain = read_zip(zip(false));
    const auto forced = read_zip(zip(true));
    ASSERT_EQ(forced.size(), plain.size());
    for (const auto& [name, entry] : plain)
    {
        ASSERT_TRUE(forced.contains(name)) << name;
        EXPECT_EQ(forced.at(name).contents, entry.contents) << name;
        EXPECT_EQ(forced.at(name).external, entry.external) << name;
        EXPECT_TRUE(forced.at(name).zip64) << name;
        EXPECT_FALSE(entry.zip64) << name;
    }
}

TEST_F(ZipTests, More_than_65535_entries_take_the_zip64_end_record)
{
    // Directories: stored entries, so the test needn't compress 66 000 files.
    fs::remove_all(root_ / "docs");
    for (int i = 0; i < 66'000; ++i)
    {
        fs::create_directory(root_ / ("d" + std::to_string(i)));
    }
    std::ofstream(root_ / "d65000" / "file") << "x";
    const std::string archive = zip();
    EXPECT_EQ(le(archive, archive.size() - 22 + 10, 2), 0xFFFFU);
    const auto entries = read_zip(archive);
    EXPECT_EQ(entries.size(), 66'001U);
    EXPECT_EQ(entries.at("d65000/file").contents, "x");
}
