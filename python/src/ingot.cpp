#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <compressor/archive/archive.h>
#include <compressor/archive/index.h>
#include <compressor/compressor.h>
#include <compressor/data/data.h>
#include <compressor/dictionary/dictionary.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
#include <compressor/longrange/longrange.h>
#include <compressor/stream/decoder.h>
#include <compressor/stream/encoder.h>

namespace py = pybind11;

namespace
{

// Raised for anything that fails; the message is compressor::describe()'s.
class IngotError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

std::shared_ptr<const compressor::Dictionary> to_dictionary(const std::optional<py::bytes>& bytes)
{
    if (!bytes)
    {
        return nullptr;
    }
    const std::string_view view(*bytes);
    auto dictionary = compressor::parse_dictionary(std::vector<uint8_t>(view.begin(), view.end()));
    if (!dictionary)
    {
        throw IngotError("a dictionary must be 1 byte to 1 MiB of content");
    }
    return std::make_shared<const compressor::Dictionary>(std::move(*dictionary));
}

unsigned checked_level(unsigned level)
{
    if (level < 1 || level > 9)
    {
        throw IngotError("level must be 1 to 9");
    }
    return level;
}

py::bytes compress(const py::bytes& data, unsigned level, unsigned threads,
                   const std::optional<py::bytes>& dictionary)
{
    checked_level(level);
    auto shared = to_dictionary(dictionary);
    std::istringstream in{std::string(data)};
    std::ostringstream out;
    std::optional<compressor::Error> error;
    {
        py::gil_scoped_release release;
        compressor::BlockEncoder encoder([level, shared] {
            return std::make_unique<compressor::Engine>(compressor::default_block_size_log2, level,
                                                        shared);
        }, compressor::default_block_size_log2, threads, shared != nullptr);
        if (auto result = encoder.encode(in, out); !result)
        {
            error = result.error();
        }
    }
    if (error)
    {
        throw IngotError(std::string(compressor::describe(*error)));
    }
    return {out.str()};
}

py::bytes decompress(const py::bytes& data, unsigned threads,
                     const std::optional<py::bytes>& dictionary)
{
    auto shared = to_dictionary(dictionary);
    std::istringstream in{std::string(data)};
    std::ostringstream out;
    std::optional<compressor::Error> error;
    {
        py::gil_scoped_release release;
        compressor::BlockDecoder decoder([shared] {
            return std::make_unique<compressor::Engine>(compressor::default_block_size_log2, 1,
                                                        shared);
        }, threads);
        if (auto result = decoder.decode(in, out); !result)
        {
            error = result.error();
        }
    }
    if (error)
    {
        throw IngotError(std::string(compressor::describe(*error)));
    }
    return {out.str()};
}

compressor::CompressorSettings settings(unsigned level, unsigned threads,
                                        const std::optional<std::filesystem::path>& dictionary)
{
    compressor::CompressorSettings result{.threads = threads, .level = level};
    if (dictionary)
    {
        auto read = compressor::read_dictionary(*dictionary);
        if (!read)
        {
            throw IngotError("cannot read the dictionary " + dictionary->string() + ": " +
                             std::string(compressor::describe(read.error())));
        }
        result.dictionary = std::make_shared<const compressor::Dictionary>(std::move(*read));
    }
    return result;
}

// The files go through Compressor, as the command line does: folders become archives, outputs are
// written through a temporary file. It reports details on stderr.
void compress_file(const std::filesystem::path& input, const std::filesystem::path& output,
                   unsigned level, unsigned threads,
                   const std::optional<std::filesystem::path>& dictionary)
{
    checked_level(level);
    compressor::Compressor compressor(settings(level, threads, dictionary));
    bool done = false;
    {
        py::gil_scoped_release release;
        done = compressor.encode(input, output);
    }
    if (!done)
    {
        throw IngotError("cannot compress " + input.string());
    }
}

void decompress_file(const std::filesystem::path& input, const std::filesystem::path& output,
                     unsigned threads, const std::optional<std::filesystem::path>& dictionary)
{
    compressor::Compressor compressor(settings(compressor::default_level, threads, dictionary));
    bool done = false;
    {
        py::gil_scoped_release release;
        done = compressor.decode(input, output);
    }
    if (!done)
    {
        throw IngotError("cannot decompress " + input.string());
    }
}

py::bytes train_dictionary(const std::vector<py::bytes>& samples, std::size_t size)
{
    std::vector<std::vector<uint8_t>> copies;
    copies.reserve(samples.size());
    for (const py::bytes& sample : samples)
    {
        const std::string_view view(sample);
        copies.emplace_back(view.begin(), view.end());
    }
    std::vector<uint8_t> dictionary;
    {
        py::gil_scoped_release release;
        dictionary = compressor::train_dictionary_file(copies, size);
    }
    return {reinterpret_cast<const char*>(dictionary.data()), dictionary.size()};
}

std::string_view entry_type(compressor::EntryType type)
{
    switch (type)
    {
    case compressor::EntryType::file:
        return "file";
    case compressor::EntryType::directory:
        return "directory";
    case compressor::EntryType::symlink:
        return "symlink";
    }
    return "unknown";
}

compressor::ArchiveInfo open_archive(std::ifstream& file, const std::filesystem::path& archive)
{
    file.open(archive, std::ios::in | std::ios::binary);
    if (!file)
    {
        throw IngotError("cannot open " + archive.string());
    }
    auto info = compressor::read_archive_index(file);
    if (!info)
    {
        throw IngotError("cannot read " + archive.string() + ": " +
                         std::string(compressor::describe(info.error())));
    }
    return std::move(*info);
}

py::list list_archive(const std::filesystem::path& archive)
{
    std::ifstream file;
    const compressor::ArchiveInfo info = open_archive(file, archive);
    py::list entries;
    for (const compressor::Entry& entry : info.index.entries)
    {
        py::dict item;
        item["path"] = entry.path;
        item["type"] = entry_type(entry.type);
        item["size"] = entry.type == compressor::EntryType::file ? entry.size : 0;
        item["mode"] = entry.mode;
        item["mtime_ns"] = entry.mtime_ns;
        if (entry.type == compressor::EntryType::symlink)
        {
            item["target"] = entry.target;
        }
        entries.append(item);
    }
    return entries;
}

py::bytes read_from_archive(const std::filesystem::path& archive, const std::string& path,
                            unsigned threads, const std::optional<py::bytes>& dictionary)
{
    auto shared = to_dictionary(dictionary);
    std::ifstream file;
    const compressor::ArchiveInfo info = open_archive(file, archive);
    const auto selected =
        compressor::select_subtree(info.index.entries, compressor::archive_path(path));
    if (!selected || selected->front().type != compressor::EntryType::file)
    {
        throw IngotError(path + " is not a file in " + archive.string());
    }
    std::ostringstream out;
    std::optional<compressor::Failure> failure;
    {
        py::gil_scoped_release release;
        compressor::BlockDecoder decoder([shared] {
            return std::make_unique<compressor::Engine>(compressor::default_block_size_log2, 1,
                                                        shared);
        }, threads);
        if (auto read = compressor::extract_file(decoder, file, info, selected->front(), out);
            !read)
        {
            failure = read.error();
        }
    }
    if (failure)
    {
        throw IngotError("cannot read " + path + ": " +
                         std::string(compressor::describe(failure->error)));
    }
    return {out.str()};
}

void extract_from_archive(const std::filesystem::path& archive, const std::string& path,
                          const std::filesystem::path& output, unsigned threads,
                          const std::optional<std::filesystem::path>& dictionary)
{
    compressor::Compressor compressor(settings(compressor::default_level, threads, dictionary));
    bool done = false;
    {
        py::gil_scoped_release release;
        done = compressor.extract(archive, path, output);
    }
    if (!done)
    {
        throw IngotError("cannot extract " + path + " from " + archive.string());
    }
}

// Like zlib.compressobj: whole blocks go out as soon as they are full, the rest with flush().
// One thread; the output is what compress() gives for the same bytes without a dictionary.
class StreamCompressor
{
public:
    StreamCompressor(unsigned level, const std::optional<py::bytes>& dictionary)
        : engine_(compressor::default_block_size_log2, checked_level(level),
                  to_dictionary(dictionary))
    {
    }

    py::bytes compress(const py::bytes& data)
    {
        if (finished_)
        {
            throw IngotError("compress() after flush()");
        }
        const std::string_view view(data);
        std::ostringstream out;
        {
            py::gil_scoped_release release;
            start(out);
            pending_.insert(pending_.end(), view.begin(), view.end());
            std::size_t at = 0;
            for (; pending_.size() - at >= block_size; at += block_size)
            {
                write(out, std::span(pending_).subspan(at, block_size));
            }
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(at));
        }
        return {out.str()};
    }

    py::bytes flush()
    {
        if (finished_)
        {
            throw IngotError("flush() twice");
        }
        std::ostringstream out;
        {
            py::gil_scoped_release release;
            start(out);
            if (!pending_.empty())
            {
                write(out, pending_);
                pending_.clear();
            }
            compressor::write_end_block(out, total_);
            finished_ = true;
        }
        return {out.str()};
    }

private:
    static constexpr std::size_t block_size = std::size_t{1} << compressor::default_block_size_log2;

    void start(std::ostream& out)
    {
        if (!started_)
        {
            compressor::write_frame_header(out, compressor::default_block_size_log2);
            started_ = true;
        }
    }

    void write(std::ostream& out, std::span<const uint8_t> bytes)
    {
        compressor::write_block(out, engine_.encode_block(bytes));
        total_ += bytes.size();
    }

    compressor::Engine engine_;
    std::vector<uint8_t> pending_;
    uint64_t total_ = 0;
    bool started_ = false;
    bool finished_ = false;
};

// Like zlib.decompressobj: each block's bytes come out once the whole block has arrived. A
// single-block frame's payload runs to the end of the file, so it comes out with flush().
class StreamDecompressor
{
public:
    explicit StreamDecompressor(const std::optional<py::bytes>& dictionary)
        : engine_(compressor::default_block_size_log2, 1, to_dictionary(dictionary))
    {
    }

    py::bytes decompress(const py::bytes& data)
    {
        const std::string_view view(data);
        if (eof_)
        {
            unused_.append(view);
            return {""};
        }
        buffer_.append(view);
        std::string out;
        std::optional<std::string> error;
        {
            py::gil_scoped_release release;
            error = advance(out);
        }
        if (error)
        {
            throw IngotError(*error);
        }
        return {out};
    }

    py::bytes flush()
    {
        std::string out;
        if (single_ && !eof_)
        {
            std::istringstream in(buffer_);
            compressor::FrameReader reader(in);
            compressor::EncodedBlock block;
            std::vector<uint8_t> decoded;
            if (!reader.read_header() || !reader.read_block(block) ||
                !engine_.decode_block(block, decoded))
            {
                throw IngotError("the single-block frame is damaged or incomplete");
            }
            out.assign(decoded.begin(), decoded.end());
            eof_ = true;
        }
        if (!eof_)
        {
            throw IngotError(std::string(compressor::describe(compressor::Error::truncated)));
        }
        return {out};
    }

    bool eof() const
    {
        return eof_;
    }

    py::bytes unused_data() const
    {
        return {unused_};
    }

private:
    static uint64_t le(std::string_view bytes, std::size_t at, std::size_t size)
    {
        uint64_t value = 0;
        for (std::size_t i = 0; i < size; ++i)
        {
            value |= uint64_t{static_cast<uint8_t>(bytes[at + i])} << (8 * i);
        }
        return value;
    }

    // Decodes every complete block in the buffer into `out`; an error message on bad input.
    std::optional<std::string> advance(std::string& out)
    {
        if (!header_)
        {
            if (buffer_.size() < compressor::frame_header_bytes + 1)
            {
                return std::nullopt;
            }
            // The window byte after the header too, for a long-range frame.
            std::istringstream in(buffer_.substr(0, compressor::frame_header_bytes + 1));
            compressor::FrameReader reader(in);
            const auto log2 = reader.read_header();
            if (!log2)
            {
                return std::string(compressor::describe(log2.error()));
            }
            if ((reader.flags() & compressor::archive_flag) != 0)
            {
                return std::string(compressor::describe(compressor::Error::unexpected_archive));
            }
            header_ = true;
            block_size_log2_ = *log2;
            if ((reader.flags() & compressor::single_block_flag) != 0)
            {
                single_ = true;
                return std::nullopt; // the whole buffer is decoded by flush()
            }
            if ((reader.flags() & compressor::long_range_flag) != 0)
            {
                history_.emplace(compressor::window_blocks(block_size_log2_, reader.window_log2()));
            }
            flags_ = reader.flags();
            buffer_.erase(0, compressor::frame_header_size(flags_));
        }
        if (single_)
        {
            return std::nullopt;
        }
        while (!buffer_.empty())
        {
            if (static_cast<uint8_t>(buffer_[0]) == 0xFF)
            {
                if (buffer_.size() < compressor::end_block_bytes)
                {
                    return std::nullopt;
                }
                if (le(buffer_, 1, 8) != total_)
                {
                    return std::string(compressor::describe(compressor::Error::corrupt_header));
                }
                eof_ = true;
                unused_ = buffer_.substr(compressor::end_block_bytes);
                buffer_.clear();
                return std::nullopt;
            }
            if (buffer_.size() < compressor::block_header_bytes)
            {
                return std::nullopt;
            }
            const uint64_t length = compressor::block_header_bytes + le(buffer_, 5, 4);
            if (buffer_.size() < length)
            {
                return std::nullopt;
            }
            // Only the last block may be shorter than the block size.
            if (short_block_)
            {
                return std::string(compressor::describe(compressor::Error::corrupt_header));
            }
            std::istringstream in(buffer_.substr(0, static_cast<std::size_t>(length)));
            compressor::FrameReader reader(in);
            reader.start_blocks(block_size_log2_, flags_);
            compressor::EncodedBlock block;
            const auto read = reader.read_block(block);
            if (!read || !*read)
            {
                return std::string(
                    compressor::describe(read ? compressor::Error::corrupt_header : read.error()));
            }
            std::vector<uint8_t> decoded;
            if (auto result = history_ ? history_->decode(engine_, block, decoded)
                                       : engine_.decode_block(block, decoded);
                !result)
            {
                return std::string(compressor::describe(result.error()));
            }
            out.append(decoded.begin(), decoded.end());
            total_ += block.size_;
            short_block_ = block.size_ < (uint64_t{1} << block_size_log2_);
            buffer_.erase(0, static_cast<std::size_t>(length));
        }
        return std::nullopt;
    }

    compressor::Engine engine_;
    std::optional<compressor::LongRangeHistory> history_;
    uint16_t flags_ = 0;
    std::string buffer_;
    std::string unused_;
    uint64_t total_ = 0;
    uint8_t block_size_log2_ = compressor::default_block_size_log2;
    bool header_ = false;
    bool single_ = false;
    bool short_block_ = false;
    bool eof_ = false;
};

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): pybind11's macro defines PyInit_ingot
PYBIND11_MODULE(ingot, module)
{
    module.doc() = "ingot: block-based compression (the ingot container), in memory and on files";
    module.attr("__version__") = COMPRESSOR_VERSION;
    module.attr("DEFAULT_LEVEL") = compressor::default_level;
    py::register_exception<IngotError>(module, "Error");

    module.def("compress", &compress, py::arg("data"), py::kw_only(),
               py::arg("level") = compressor::default_level, py::arg("threads") = 0,
               py::arg("dictionary") = py::none(),
               "Compress bytes into an ingot container. threads=0: all cores.");
    module.def("decompress", &decompress, py::arg("data"), py::kw_only(), py::arg("threads") = 0,
               py::arg("dictionary") = py::none(),
               "Decompress an ingot container (not a folder archive) back into bytes.");
    module.def("compress_file", &compress_file, py::arg("input"), py::arg("output"), py::kw_only(),
               py::arg("level") = compressor::default_level, py::arg("threads") = 0,
               py::arg("dictionary") = py::none(),
               "Compress a file, or a folder into an archive; dictionary is a file's path.");
    module.def("decompress_file", &decompress_file, py::arg("input"), py::arg("output"),
               py::kw_only(), py::arg("threads") = 0, py::arg("dictionary") = py::none(),
               "Decompress an ingot file, or an archive into a new folder.");
    module.def("list", &list_archive, py::arg("archive"),
               "The entries of a folder archive: dicts with path, type, size, mode, mtime_ns "
               "and, for symlinks, target.");
    module.def("read", &read_from_archive, py::arg("archive"), py::arg("path"), py::kw_only(),
               py::arg("threads") = 0, py::arg("dictionary") = py::none(),
               "The contents of one file in a folder archive, decoding only its blocks.");
    module.def("extract", &extract_from_archive, py::arg("archive"), py::arg("path"),
               py::arg("output"), py::kw_only(), py::arg("threads") = 0,
               py::arg("dictionary") = py::none(),
               "Extract a file, directory or symlink from a folder archive to `output`.");
    py::class_<StreamCompressor>(module, "Compressor",
                                 "Incremental compression, like zlib.compressobj().")
        .def(py::init<unsigned, const std::optional<py::bytes>&>(), py::kw_only(),
             py::arg("level") = compressor::default_level, py::arg("dictionary") = py::none())
        .def("compress", &StreamCompressor::compress, py::arg("data"),
             "Returns the container's bytes that are ready (whole blocks).")
        .def("flush", &StreamCompressor::flush, "Returns the rest; call once, at the end.");
    py::class_<StreamDecompressor>(module, "Decompressor",
                                   "Incremental decompression, like zlib.decompressobj().")
        .def(py::init<const std::optional<py::bytes>&>(), py::kw_only(),
             py::arg("dictionary") = py::none())
        .def("decompress", &StreamDecompressor::decompress, py::arg("data"),
             "Returns the bytes of every block now complete.")
        .def("flush", &StreamDecompressor::flush,
             "Returns what is left (a single-block frame's bytes); fails unless the end arrived.")
        .def_property_readonly("eof", &StreamDecompressor::eof)
        .def_property_readonly("unused_data", &StreamDecompressor::unused_data);
    module.def("train_dictionary", &train_dictionary, py::arg("samples"), py::kw_only(),
               py::arg("size") = compressor::default_dictionary_bytes,
               "Train a dictionary for compressing many small, similar inputs.");
}
