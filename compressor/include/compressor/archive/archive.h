#ifndef Archive_h
#define Archive_h

#include <expected>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <compressor/archive/index.h>
#include <compressor/error.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace compressor
{

struct Failure
{
    Error error;
    std::string detail; // the path and system message when a file is to blame
};

// Walks `root` without following symlinks: each directory before its contents, siblings in byte
// order of their names. Skips `exclude` (when it exists) and anything that isn't a regular file,
// directory or symlink, naming each skipped path on `warnings`. File sizes are what the walk saw;
// write_archive() records what it actually reads.
std::expected<std::vector<Entry>, Failure> scan_tree(const std::filesystem::path& root,
                                                     const std::filesystem::path& exclude,
                                                     std::ostream& warnings);

// The container with the archive flag, the files' bytes back to back as its data, and the index.
std::expected<void, Failure> write_archive(IBlockEncoder& encoder,
                                           const std::filesystem::path& root,
                                           std::vector<Entry> entries, std::ostream& out);

// Reads the header of `in` and puts the stream back at its start.
bool is_archive(std::istream& in);

// `in` must be able to seek. Extracts into `directory`, which must exist and be empty; modes and
// modification times are set after everything is written, directories last.
std::expected<void, Failure> extract_archive(IBlockDecoder& decoder, std::istream& in,
                                             const std::filesystem::path& directory);

// `path` as the index spells it: '/'-separated and normalised, without a trailing '/'.
std::string archive_path(std::string_view path);

// The entry at `path` and, for a directory, everything under it, with paths relative to `path`,
// so the entry itself has an empty path. Empty when there is no such entry.
std::optional<std::vector<Entry>> select_subtree(std::span<const Entry> entries,
                                                 std::string_view path);

// The functions below take `info` from read_archive_index(in) and decode only the blocks that
// hold the files they need.

// Extracts `entries`, from info.index or select_subtree(), into `directory`, which must exist and
// be empty. An entry with an empty path is `directory` itself and only gets its mode and time.
std::expected<void, Failure> extract_entries(IBlockDecoder& decoder, std::istream& in,
                                             const ArchiveInfo& info,
                                             std::span<const Entry> entries,
                                             const std::filesystem::path& directory);

// Writes the contents of a file entry to `out`, checking its CRC.
std::expected<void, Failure> extract_file(IBlockDecoder& decoder, std::istream& in,
                                          const ArchiveInfo& info, const Entry& file,
                                          std::ostream& out);

// Decodes every block and checks every block and file CRC, writing nothing.
std::expected<void, Failure> verify_archive(IBlockDecoder& decoder, std::istream& in,
                                            const ArchiveInfo& info);

// One line per entry, directories ending in '/'. `details` adds the type and permissions, the size
// (a symlink's is its target's length) and the modification time in UTC, and symlink targets.
void list_archive(const ArchiveInfo& info, bool details, std::ostream& out);

} // namespace compressor

#endif /* Archive_h */
