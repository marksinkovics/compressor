#ifndef Archive_h
#define Archive_h

#include <expected>
#include <filesystem>
#include <iosfwd>
#include <string>
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

} // namespace compressor

#endif /* Archive_h */
