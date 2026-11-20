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
constexpr uint32_t zip64_end_signature = 0x06064B50;
constexpr uint32_t zip64_locator_signature = 0x07064B50;
constexpr uint16_t zip64_extra_id = 0x0001;
constexpr uint16_t version_zip64 = 45;
constexpr uint16_t version_made_by_zip64 = (3U << 8U) | 45U;
constexpr uint64_t max_32 = 0xFFFFFFFFU; // also the "see the Zip64 field" marker
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

    void u64(uint64_t value)
    {
        u32(value & 0xFFFFFFFFU);
        u32(value >> 32U);
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
    bool zip64 = false; // the local header and data descriptor use Zip64
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

} // namespace

std::expected<void, Failure> write_zip(const fs::path& root, const std::vector<Entry>& entries,
                                       std::ostream& out, const Lz77Options& options,
                                       bool force_zip64)
{
    std::vector<Central> central;
    uint64_t offset = 0;
    const auto emit = [&](const Bytes& bytes) {
        out.write(bytes.str().data(), static_cast<std::streamsize>(bytes.str().size()));
        offset += bytes.str().size();
    };

    for (const Entry& entry : entries)
    {
        Central record{.zip64 = false,
                       .name = entry.path,
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
            // The size isn't known until the data is written, so decide from the scan; a file
            // that grows past 4 GiB after it fails below.
            record.zip64 = force_zip64 || entry.size >= max_32;
            break;
        }
        if (entry.type != EntryType::file)
        {
            record.crc = crc32({reinterpret_cast<const uint8_t*>(content.data()), content.size()});
            record.compressed = content.size();
            record.size = content.size();
        }

        Bytes local;
        local.u32(local_header_signature);
        local.u16(record.zip64 ? version_zip64 : version_needed);
        local.u16(record.flags);
        local.u16(record.method);
        local.u16(record.time.time);
        local.u16(record.time.date);
        local.u32(record.crc);
        local.u32(record.zip64 ? max_32 : record.compressed);
        local.u32(record.zip64 ? max_32 : record.size);
        local.u16(static_cast<uint32_t>(record.name.size()));
        local.u16(record.zip64 ? 20 : 0);
        local.text(record.name);
        if (record.zip64)
        {
            // Sizes follow in the data descriptor; these say the descriptor's are 8 bytes.
            local.u16(zip64_extra_id);
            local.u16(16);
            local.u64(0);
            local.u64(0);
        }
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
            if (!record.zip64 && (result->input_bytes >= max_32 || result->output_bytes >= max_32))
            {
                return std::unexpected(
                    Failure{.error = Error::file_failed,
                            .detail = path.string() + ": grew past 4 GiB while being archived"});
            }
            offset += result->output_bytes;
            record.crc = result->crc;
            record.compressed = result->output_bytes;
            record.size = result->input_bytes;
            Bytes descriptor;
            descriptor.u32(descriptor_signature);
            descriptor.u32(record.crc);
            if (record.zip64)
            {
                descriptor.u64(record.compressed);
                descriptor.u64(record.size);
            }
            else
            {
                descriptor.u32(record.compressed);
                descriptor.u32(record.size);
            }
            emit(descriptor);
        }
        central.push_back(std::move(record));
    }

    const uint64_t central_offset = offset;
    for (const Central& record : central)
    {
        // Fields that don't fit say 0xFFFFFFFF and go into a Zip64 extra field, in this order.
        Bytes extra;
        const auto field = [&](uint64_t value) -> uint32_t {
            if (value >= max_32 || force_zip64)
            {
                extra.u64(value);
                return static_cast<uint32_t>(max_32);
            }
            return static_cast<uint32_t>(value);
        };
        const uint32_t size = field(record.size);
        const uint32_t compressed = field(record.compressed);
        const uint32_t local_offset = field(record.offset);
        const bool zip64 = !extra.str().empty();

        Bytes header;
        header.u32(central_header_signature);
        header.u16(zip64 ? version_made_by_zip64 : version_made_by);
        header.u16(zip64 ? version_zip64 : version_needed);
        header.u16(record.flags);
        header.u16(record.method);
        header.u16(record.time.time);
        header.u16(record.time.date);
        header.u32(record.crc);
        header.u32(compressed);
        header.u32(size);
        header.u16(static_cast<uint32_t>(record.name.size()));
        header.u16(zip64 ? static_cast<uint32_t>(4 + extra.str().size()) : 0);
        header.u16(0); // comment
        header.u16(0); // disk
        header.u16(0); // internal attributes
        header.u32(record.external);
        header.u32(local_offset);
        header.text(record.name);
        if (zip64)
        {
            header.u16(zip64_extra_id);
            header.u16(static_cast<uint32_t>(extra.str().size()));
            header.text(extra.str());
        }
        emit(header);
    }

    const uint64_t central_size = offset - central_offset;
    const bool zip64_end = force_zip64 || central.size() > max_entries || central_size >= max_32 ||
                           central_offset >= max_32;
    if (zip64_end)
    {
        const uint64_t record_offset = offset;
        Bytes end64;
        end64.u32(zip64_end_signature);
        end64.u64(44); // the record's size after this field
        end64.u16(version_made_by_zip64);
        end64.u16(version_zip64);
        end64.u32(0); // this disk
        end64.u32(0); // the disk with the central directory
        end64.u64(central.size());
        end64.u64(central.size());
        end64.u64(central_size);
        end64.u64(central_offset);
        end64.u32(zip64_locator_signature);
        end64.u32(0);
        end64.u64(record_offset);
        end64.u32(1); // disks
        emit(end64);
    }
    const auto clamp = [zip64_end](uint64_t value, uint64_t limit) {
        return zip64_end && value >= limit ? limit : value;
    };
    Bytes end;
    end.u32(end_signature);
    end.u16(0);
    end.u16(0);
    end.u16(static_cast<uint32_t>(zip64_end ? max_entries : central.size()));
    end.u16(static_cast<uint32_t>(zip64_end ? max_entries : central.size()));
    end.u32(zip64_end ? max_32 : clamp(central_size, max_32));
    end.u32(zip64_end ? max_32 : clamp(central_offset, max_32));
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
