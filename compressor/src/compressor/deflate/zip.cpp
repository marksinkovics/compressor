#include <compressor/deflate/zip.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <compressor/archive/archive.h>
#include <compressor/archive/index.h>
#include <compressor/checksum/crc32.h>
#include <compressor/deflate/deflate.h>
#include <compressor/error.h>

namespace compressor
{

namespace
{

namespace fs = std::filesystem;

constexpr uint32_t local_header_signature = 0x04034B50;
constexpr uint32_t descriptor_signature = 0x08074B50;
constexpr uint32_t central_header_signature = 0x02014B50;
constexpr uint32_t end_signature = 0x06054B50;
constexpr uint16_t version_needed = 20;
constexpr uint16_t version_made_by = (3U << 8U) | 20U; // Unix
constexpr uint16_t utf8_flag = 0x0800;
constexpr uint16_t descriptor_flag = 0x0008;
constexpr uint16_t stored = 0;
constexpr uint16_t deflated = 8;
constexpr uint32_t regular_type = 0100000;
constexpr uint32_t directory_type = 0040000;
constexpr uint32_t symlink_type = 0120000;
constexpr uint32_t dos_directory = 0x10;
constexpr uint64_t max_32 = 0xFFFFFFFFU;
constexpr std::size_t max_entries = 0xFFFF;

class Bytes
{
public:
    void u16(uint32_t value)
    {
        bytes_.push_back(static_cast<char>(value & 0xFFU));
        bytes_.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    }

    void u32(uint64_t value)
    {
        u16(static_cast<uint32_t>(value & 0xFFFFU));
        u16(static_cast<uint32_t>((value >> 16U) & 0xFFFFU));
    }

    void text(std::string_view value)
    {
        bytes_.append(value);
    }

    const std::string& str() const
    {
        return bytes_;
    }

private:
    std::string bytes_;
};

struct DosTime
{
    uint16_t time = 0;
    uint16_t date = 0;
};

// Local time, in 2-second steps, from 1980 on (earlier times become 1980-01-01).
DosTime dos_time(int64_t mtime_ns)
{
    const auto seconds = static_cast<std::time_t>(mtime_ns / 1'000'000'000);
    std::tm local{};
    if (localtime_r(&seconds, &local) == nullptr || local.tm_year < 80)
    {
        return {.time = 0, .date = (1U << 5U) | 1U};
    }
    const auto field = [](int value) {
        return static_cast<unsigned>(value);
    };
    return {.time = static_cast<uint16_t>((field(local.tm_hour) << 11U) |
                                          (field(local.tm_min) << 5U) | field(local.tm_sec / 2)),
            .date = static_cast<uint16_t>((field(local.tm_year - 80) << 9U) |
                                          (field(local.tm_mon + 1) << 5U) | field(local.tm_mday))};
}

struct Central
{
    std::string name;
    uint16_t flags = 0;
    uint16_t method = stored;
    DosTime time;
    uint32_t crc = 0;
    uint64_t compressed = 0;
    uint64_t size = 0;
    uint32_t external = 0;
    uint64_t offset = 0;
};

Failure too_large(const fs::path& path)
{
    return {.error = Error::file_failed,
            .detail = path.string() + ": too large for a zip archive without Zip64"};
}

} // namespace

std::expected<void, Failure> write_zip(const fs::path& root, const std::vector<Entry>& entries,
                                       std::ostream& out, const Lz77Options& options)
{
    if (entries.size() > max_entries)
    {
        return std::unexpected(
            Failure{.error = Error::file_failed, .detail = "more than 65535 entries need Zip64"});
    }
    std::vector<Central> central;
    uint64_t offset = 0;
    const auto emit = [&](const Bytes& bytes) {
        out.write(bytes.str().data(), static_cast<std::streamsize>(bytes.str().size()));
        offset += bytes.str().size();
    };

    for (const Entry& entry : entries)
    {
        Central record{.name = entry.path,
                       .flags = utf8_flag,
                       .method = stored,
                       .time = dos_time(entry.mtime_ns),
                       .crc = 0,
                       .compressed = 0,
                       .size = 0,
                       .external = 0,
                       .offset = offset};
        std::string content; // stored entries only
        switch (entry.type)
        {
        case EntryType::directory:
            record.name += '/';
            record.external = ((directory_type | entry.mode) << 16U) | dos_directory;
            break;
        case EntryType::symlink:
            content = entry.target;
            record.external = (symlink_type | entry.mode) << 16U;
            break;
        case EntryType::file:
            record.flags |= descriptor_flag;
            record.method = deflated;
            record.external = (regular_type | entry.mode) << 16U;
            break;
        }
        if (entry.type != EntryType::file)
        {
            record.crc = crc32({reinterpret_cast<const uint8_t*>(content.data()), content.size()});
            record.compressed = content.size();
            record.size = content.size();
        }
        if (offset > max_32)
        {
            return std::unexpected(too_large(root / entry.path));
        }

        Bytes local;
        local.u32(local_header_signature);
        local.u16(version_needed);
        local.u16(record.flags);
        local.u16(record.method);
        local.u16(record.time.time);
        local.u16(record.time.date);
        local.u32(record.crc);
        local.u32(record.compressed);
        local.u32(record.size);
        local.u16(static_cast<uint32_t>(record.name.size()));
        local.u16(0);
        local.text(record.name);
        local.text(content);
        emit(local);

        if (entry.type == EntryType::file)
        {
            const fs::path path = root / entry.path;
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                return std::unexpected(Failure{.error = Error::file_failed,
                                               .detail = path.string() + ": cannot be read"});
            }
            const auto result = deflate_stream(file, out, options);
            if (!result)
            {
                return std::unexpected(Failure{.error = result.error(), .detail = path.string()});
            }
            if (result->input_bytes > max_32 || result->output_bytes > max_32)
            {
                return std::unexpected(too_large(path));
            }
            offset += result->output_bytes;
            record.crc = result->crc;
            record.compressed = result->output_bytes;
            record.size = result->input_bytes;
            Bytes descriptor;
            descriptor.u32(descriptor_signature);
            descriptor.u32(record.crc);
            descriptor.u32(record.compressed);
            descriptor.u32(record.size);
            emit(descriptor);
        }
        central.push_back(std::move(record));
    }

    const uint64_t central_offset = offset;
    for (const Central& record : central)
    {
        Bytes header;
        header.u32(central_header_signature);
        header.u16(version_made_by);
        header.u16(version_needed);
        header.u16(record.flags);
        header.u16(record.method);
        header.u16(record.time.time);
        header.u16(record.time.date);
        header.u32(record.crc);
        header.u32(record.compressed);
        header.u32(record.size);
        header.u16(static_cast<uint32_t>(record.name.size()));
        header.u16(0); // extra
        header.u16(0); // comment
        header.u16(0); // disk
        header.u16(0); // internal attributes
        header.u32(record.external);
        header.u32(record.offset);
        header.text(record.name);
        emit(header);
    }
    if (offset > max_32)
    {
        return std::unexpected(Failure{.error = Error::file_failed,
                                       .detail = "the archive is too large without Zip64"});
    }
    Bytes end;
    end.u32(end_signature);
    end.u16(0);
    end.u16(0);
    end.u16(static_cast<uint32_t>(central.size()));
    end.u16(static_cast<uint32_t>(central.size()));
    end.u32(offset - central_offset);
    end.u32(central_offset);
    end.u16(0);
    emit(end);
    out.flush();
    if (!out)
    {
        return std::unexpected(Failure{.error = Error::write_failed, .detail = {}});
    }
    return {};
}

} // namespace compressor
