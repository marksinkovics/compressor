#ifndef Zip_h
#define Zip_h

#include <expected>
#include <filesystem>
#include <iosfwd>
#include <vector>

#include <compressor/archive/archive.h>
#include <compressor/archive/index.h>
#include <compressor/lz77/lz77.h>

namespace compressor
{

// A zip archive (PKWARE APPNOTE) of `entries` from scan_tree(root): files Deflate-compressed with
// a data descriptor after each, so `out` needn't seek; directories and symlinks stored. Unix modes
// and the symlink type go in the external attributes. No Zip64: files and offsets must stay under
// 4 GiB and the count under 65 536.
std::expected<void, Failure> write_zip(const std::filesystem::path& root,
                                       const std::vector<Entry>& entries, std::ostream& out,
                                       const Lz77Options& options = {});

} // namespace compressor

#endif /* Zip_h */
