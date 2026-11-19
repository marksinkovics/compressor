#include <compressor/cli.h>

#include <cstdlib>
#include <string>
#include <string_view>

#include <CLI/CLI.hpp>

#include <compressor/compressor.h>

namespace compressor
{

namespace
{

struct Paths
{
    std::string input{standard_stream};
    std::string output{standard_stream};
};

std::string display_name(const std::string& path, std::string_view standard_name)
{
    return path == standard_stream ? std::string(standard_name) : path;
}

::CLI::App* add_command(::CLI::App& app, const std::string& name, const std::string& description,
                        Paths& paths)
{
    auto* command = app.add_subcommand(name, description);
    const ::CLI::Validator existing_file_or_stdin([](std::string& path) {
        return path == standard_stream ? std::string{} : ::CLI::ExistingFile(path);
    }, "FILE");
    command->add_option("-i,--input", paths.input, "Input file; - or omitted: standard input")
        ->check(existing_file_or_stdin);
    command->add_option("-o,--output", paths.output, "Output file; - or omitted: standard output");
    return command;
}

} // namespace

CLI::CLI(std::unique_ptr<ICompressor> compressor, std::ostream& out, std::ostream& err)
    : compressor_(std::move(compressor))
    , out_(out)
    , err_(err)
{
}

int CLI::run(std::span<const char* const> args)
{
    ::CLI::App app("Huffman-coding compressor", "compressor");
    app.set_version_flag("--version", COMPRESSOR_VERSION);
    app.require_subcommand(1);

    Paths encode_paths;
    Paths decode_paths;
    auto* encode = add_command(app, "encode", "Compress a file", encode_paths);
    add_command(app, "decode", "Decompress a file", decode_paths);

    try
    {
        app.parse(static_cast<int>(args.size()), args.data());
    }
    catch (const ::CLI::ParseError& e)
    {
        return app.exit(e, out_, err_);
    }

    if (encode->parsed())
    {
        if (compressor_->encode(encode_paths.input, encode_paths.output))
            return EXIT_SUCCESS;
        err_ << "Encoding " << display_name(encode_paths.input, "standard input") << " failed\n";
        return EXIT_FAILURE;
    }

    if (compressor_->decode(decode_paths.input, decode_paths.output))
        return EXIT_SUCCESS;
    err_ << "Decoding " << display_name(decode_paths.input, "standard input") << " failed\n";
    return EXIT_FAILURE;
}

} // namespace compressor
