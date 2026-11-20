#include <cstddef>
#include <cstdint>
#include <filesystem>
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

#include <compressor/compressor.h>
#include <compressor/dictionary/dictionary.h>
#include <compressor/engine/engine.h>
#include <compressor/error.h>
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

void check_level(unsigned level)
{
    if (level < 1 || level > 9)
    {
        throw IngotError("level must be 1 to 9");
    }
}

py::bytes compress(const py::bytes& data, unsigned level, unsigned threads,
                   const std::optional<py::bytes>& dictionary)
{
    check_level(level);
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
    check_level(level);
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
    module.def("train_dictionary", &train_dictionary, py::arg("samples"), py::kw_only(),
               py::arg("size") = compressor::default_dictionary_bytes,
               "Train a dictionary for compressing many small, similar inputs.");
}
