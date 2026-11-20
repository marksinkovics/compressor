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

class ThreadPool;

// A zip archive (PKWARE APPNOTE) of `entries` from scan_tree(root): files Deflate-compressed with
// a data descriptor after each, so `out` needn't seek; directories and symlinks stored. Unix modes
// and the symlink type go in the external attributes. Zip64 records appear where something doesn't
// fit in 32 bits (a file of 4 GiB or more, offsets past 4 GiB) or there are more than 65 535
// entries; `force_zip64` writes them everywhere, for testing. Files are deflated on `pool` when
// given, with the same output as without.
std::expected<void, Failure> write_zip(const std::filesystem::path& root,
                                       const std::vector<Entry>& entries, std::ostream& out,
                                       const Lz77Options& options = {}, ThreadPool* pool = nullptr,
                                       bool force_zip64 = false);

} // namespace compressor

#endif /* Zip_h */
