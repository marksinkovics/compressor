#ifndef Compressor_h
#define Compressor_h

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <compressor/search/search.h>
#include <compressor/stats/stats.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace compressor
{

// As an input or output path, the standard input or output; "./-" names a file called "-".
inline constexpr std::string_view standard_stream = "-";

struct StandardStreams
{
    std::istream& in;
    std::ostream& out;
    std::ostream& err;
};

// Compression level when none is given: LZ77 + Huffman with zlib's level-6 match search.
inline constexpr unsigned default_level = 6;

struct CompressorSettings
{
    unsigned threads = 0; // 0: default_thread_count()
    unsigned level = default_level;
    bool search_index = false; // folder archives get an FM-index for search()
};

// stdin and stdout through StdioInputBuf and StdioOutputBuf, and std::cerr.
StandardStreams process_streams();

enum class OutputFormat : uint8_t
{
    ingot,
    gzip, // one file, RFC 1952
    zip,  // a folder
};

struct StatsOptions
{
    StatsReport report;
    // Where to write the Huffman code tree as Graphviz: none when empty; on the standard output,
    // instead of the report, for "-".
    std::filesystem::path graph;
};

class ICompressor
{
public:
    virtual ~ICompressor() = default;
    virtual bool encode(const std::filesystem::path& input,
                        const std::filesystem::path& output) = 0;
    virtual bool decode(const std::filesystem::path& input,
                        const std::filesystem::path& output) = 0;
    // encode() in another format; gzip takes a file or the standard input, zip a folder.
    virtual bool encode_as(const std::filesystem::path& input, const std::filesystem::path& output,
                           OutputFormat format) = 0;
    // Prints the paths in a folder archive to the standard output; `details` adds the types,
    // permissions, sizes and times.
    virtual bool list(const std::filesystem::path& archive, bool details) = 0;
    // Writes the file, directory or symlink at `path` in a folder archive to `output`, which must
    // not exist unless `path` is a file; only a file can go to the standard output.
    virtual bool extract(const std::filesystem::path& archive, const std::string& path,
                         const std::filesystem::path& output) = 0;
    // Decodes everything and checks every checksum, writing nothing but a line on success.
    virtual bool verify(const std::filesystem::path& input) = 0;
    // Prints what the input holds and how well it compresses; any file, an ingot file is decoded.
    virtual bool stats(const std::filesystem::path& input, const StatsOptions& options) = 0;
    // Prints the lines of an ingot file, or of the files in a folder archive (only those at
    // `paths` when given), that contain the pattern.
    virtual SearchResult grep(const std::filesystem::path& input, const SearchOptions& options,
                              const std::vector<std::string>& paths) = 0;
    // Prints `path:offset` for every occurrence of `pattern` in a folder archive's files, or with
    // `count` each file's number of occurrences, from the archive's search index without decoding.
    virtual SearchResult search(const std::filesystem::path& archive, const std::string& pattern,
                                bool count) = 0;
};

// Writes a regular output through a temporary file next to it and renames that on success, so a
// failure leaves an existing output untouched and encoding a file onto itself works. Devices and
// pipes such as /dev/null are written in place, and so is the standard output, where a failed
// decode may leave the blocks before the failure. A directory input is encoded as a folder
// archive, and an archive file decodes into a new directory, built next to it and renamed.
class Compressor : public ICompressor
{
public:
    // Block coders with default_thread_count() threads at default_level.
    Compressor();
    explicit Compressor(CompressorSettings settings);
    Compressor(std::unique_ptr<IBlockEncoder> encoder, std::unique_ptr<IBlockDecoder> decoder,
               StandardStreams streams = process_streams(), bool search_index = false);
    ~Compressor() override = default;

    bool encode(const std::filesystem::path& input, const std::filesystem::path& output) override;
    bool decode(const std::filesystem::path& input, const std::filesystem::path& output) override;
    bool encode_as(const std::filesystem::path& input, const std::filesystem::path& output,
                   OutputFormat format) override;
    bool list(const std::filesystem::path& archive, bool details) override;
    bool extract(const std::filesystem::path& archive, const std::string& path,
                 const std::filesystem::path& output) override;
    bool verify(const std::filesystem::path& input) override;
    bool stats(const std::filesystem::path& input, const StatsOptions& options) override;
    SearchResult grep(const std::filesystem::path& input, const SearchOptions& options,
                      const std::vector<std::string>& paths) override;
    SearchResult search(const std::filesystem::path& archive, const std::string& pattern,
                        bool count) override;

private:
    bool encode_folder(const std::filesystem::path& input, const std::filesystem::path& output);
    bool decode_folder(const std::filesystem::path& input, const std::filesystem::path& output);

    std::unique_ptr<IBlockEncoder> encoder_;
    std::unique_ptr<IBlockDecoder> decoder_;
    StandardStreams streams_;
    bool search_index_;
    // Threads and level for the gzip and zip formats; one thread unless given.
    CompressorSettings settings_{.threads = 1, .level = default_level, .search_index = false};
};

} // namespace compressor

#endif /* Compressor_h */
