#ifndef ArchiveMetadata_h
#define ArchiveMetadata_h

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <system_error>

#include <compressor/archive/index.h>

namespace compressor
{

struct FileMetadata
{
    std::optional<EntryType> type; // empty for devices, pipes and sockets
    uint16_t mode = 0;
    int64_t mtime_ns = 0;
    uint64_t size = 0;
    uint64_t device = 0;
    uint64_t inode = 0;
};

// Doesn't follow a symlink at `path` unless `follow`.
std::expected<FileMetadata, std::error_code> read_metadata(const std::filesystem::path& path,
                                                           bool follow = false);
std::error_code set_mode(const std::filesystem::path& path, uint16_t mode);
// Sets the modification time of a symlink itself, not of its target.
std::error_code set_mtime(const std::filesystem::path& path, int64_t mtime_ns);

} // namespace compressor

#endif /* ArchiveMetadata_h */
