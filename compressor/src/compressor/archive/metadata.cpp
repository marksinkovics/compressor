#include <compressor/archive/metadata.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <expected>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#error "compressor/archive/metadata.cpp has no Windows port yet (docs/roadmap.md)"
#endif

#include <fcntl.h>
#include <sys/stat.h>

namespace compressor
{

namespace
{

constexpr int64_t nanoseconds_per_second = 1'000'000'000;

std::error_code last_error()
{
    return {errno, std::generic_category()};
}

int64_t mtime_of(const struct stat& status)
{
#if defined(__APPLE__)
    const timespec& time = status.st_mtimespec;
#else
    const timespec& time = status.st_mtim;
#endif
    return (static_cast<int64_t>(time.tv_sec) * nanoseconds_per_second) + time.tv_nsec;
}

} // namespace

std::expected<FileMetadata, std::error_code> read_metadata(const std::filesystem::path& path,
                                                           bool follow)
{
    struct stat status{};
    if ((follow ? ::stat(path.c_str(), &status) : ::lstat(path.c_str(), &status)) != 0)
    {
        return std::unexpected(last_error());
    }
    FileMetadata metadata;
    if (S_ISREG(status.st_mode))
    {
        metadata.type = EntryType::file;
    }
    else if (S_ISDIR(status.st_mode))
    {
        metadata.type = EntryType::directory;
    }
    else if (S_ISLNK(status.st_mode))
    {
        metadata.type = EntryType::symlink;
    }
    metadata.mode = static_cast<uint16_t>(status.st_mode & max_mode);
    metadata.mtime_ns = mtime_of(status);
    metadata.size = static_cast<uint64_t>(status.st_size);
    metadata.device = static_cast<uint64_t>(status.st_dev);
    metadata.inode = static_cast<uint64_t>(status.st_ino);
    return metadata;
}

std::error_code set_mode(const std::filesystem::path& path, uint16_t mode)
{
    if (::chmod(path.c_str(), mode) != 0)
    {
        return last_error();
    }
    return {};
}

std::error_code set_mtime(const std::filesystem::path& path, int64_t mtime_ns)
{
    // Floor division, so times before 1970 keep tv_nsec in [0, 1e9).
    int64_t seconds = mtime_ns / nanoseconds_per_second;
    int64_t nanoseconds = mtime_ns % nanoseconds_per_second;
    if (nanoseconds < 0)
    {
        --seconds;
        nanoseconds += nanoseconds_per_second;
    }
    const std::array<timespec, 2> times{{
        {.tv_sec = 0, .tv_nsec = UTIME_OMIT},
        {.tv_sec = static_cast<time_t>(seconds), .tv_nsec = static_cast<long>(nanoseconds)},
    }};
    if (::utimensat(AT_FDCWD, path.c_str(), times.data(), AT_SYMLINK_NOFOLLOW) != 0)
    {
        return last_error();
    }
    return {};
}

} // namespace compressor
