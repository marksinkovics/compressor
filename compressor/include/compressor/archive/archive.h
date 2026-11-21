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
#include <compressor/range/range.h>
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

// The container with the archive flag, the files' bytes back to back as its data, and the index;
// with `search_index`, an FM-index section after it (fmindex/fmindex.h), built from the same read.
// Orders entries for a solid archive: directories first (in their order, so parents still come
// before their contents), then symlinks, then files grouped by extension (ignoring case) and then
// by name, so that similar files sit next to each other in the compressed stream. Stable.
void order_for_compression(std::vector<Entry>& entries);

std::expected<void, Failure> write_archive(IBlockEncoder& encoder,
                                           const std::filesystem::path& root,
                                           std::vector<Entry> entries, std::ostream& out,
                                           bool search_index = false);

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

// Writes bytes `range` of a file entry to `out`, decoding only the blocks that hold them (each
// checked against its block CRC; the file's CRC needs the whole file). bad_range when the range
// starts past the file's end; a `to` past it stops at the end.
std::expected<void, Failure> extract_range(IBlockDecoder& decoder, std::istream& in,
                                           const ArchiveInfo& info, const Entry& file,
                                           ByteRange range, std::ostream& out);

// Writes the contents of a file entry to `out`, checking its CRC.
std::expected<void, Failure> extract_file(IBlockDecoder& decoder, std::istream& in,
                                          const ArchiveInfo& info, const Entry& file,
                                          std::ostream& out);

// The entries at `paths` and everything under them, with their full paths, in entry order; all
// of them when `paths` is empty. Fails with the first path that names no entry.
std::expected<std::vector<Entry>, std::string> select_paths(std::span<const Entry> entries,
                                                            std::span<const std::string> paths);

// Receives the contents of one file after another.
class FileVisitor
{
public:
    virtual ~FileVisitor() = default;
    // `paths`: the file and any duplicates that share its contents.
    virtual void begin(std::span<const std::filesystem::path> paths) = 0;
    virtual void data(std::string_view bytes) = 0;
    // Only once the contents match the file's CRC.
    virtual void end() = 0;
};

// Hands the contents of the files among `entries` to `visitor`, in data order and each shared
// content once.
std::expected<void, Failure> visit_files(IBlockDecoder& decoder, std::istream& in,
                                         const ArchiveInfo& info, std::span<const Entry> entries,
                                         FileVisitor& visitor);

// Decodes every block and checks every block and file CRC, writing nothing.
std::expected<void, Failure> verify_archive(IBlockDecoder& decoder, std::istream& in,
                                            const ArchiveInfo& info);

// One line per entry, directories ending in '/'. `details` adds the type and permissions, the size
// (a symlink's is its target's length) and the modification time in UTC, and symlink targets.
void list_archive(const ArchiveInfo& info, bool details, std::ostream& out);

} // namespace compressor

#endif /* Archive_h */
