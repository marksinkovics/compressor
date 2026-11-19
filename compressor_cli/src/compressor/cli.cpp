#include <compressor/cli.h>

#include <cstdlib>
#include <string>

#include <CLI/CLI.hpp>

#include <compressor/compressor.h>

namespace compressor
{

namespace
{

struct Paths
{
    std::string input;
    std::string output;
};

::CLI::App* add_command(::CLI::App& app, const std::string& name, const std::string& description,
                        Paths& paths)
{
    auto* command = app.add_subcommand(name, description);
    command->add_option("-i,--input", paths.input, "Input file")
        ->required()
        ->check(::CLI::ExistingFile);
    command->add_option("-o,--output", paths.output, "Output file")->required();
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
        err_ << "Encoding " << encode_paths.input << " failed\n";
        return EXIT_FAILURE;
    }

    if (compressor_->decode(decode_paths.input, decode_paths.output))
        return EXIT_SUCCESS;
    err_ << "Decoding " << decode_paths.input << " failed\n";
    return EXIT_FAILURE;
}

} // namespace compressor
