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

struct Settings
{
    std::string input{standard_stream};
    std::string output{standard_stream};
    unsigned threads = 0;
};

std::string display_name(const std::string& path, std::string_view standard_name)
{
    return path == standard_stream ? std::string(standard_name) : path;
}

::CLI::App* add_command(::CLI::App& app, const std::string& name, const std::string& description,
                        Settings& settings)
{
    auto* command = app.add_subcommand(name, description);
    const ::CLI::Validator existing_file_or_stdin([](std::string& path) {
        return path == standard_stream ? std::string{} : ::CLI::ExistingFile(path);
    }, "FILE");
    command->add_option("-i,--input", settings.input, "Input file; - or omitted: standard input")
        ->check(existing_file_or_stdin);
    command->add_option("-o,--output", settings.output,
                        "Output file; - or omitted: standard output");
    command->add_option("-T,--threads", settings.threads, "Worker threads; 0 or omitted: all cores")
        ->check(::CLI::NonNegativeNumber);
    return command;
}

} // namespace

CLI::CLI(CompressorFactory make_compressor, std::ostream& out, std::ostream& err)
    : make_compressor_(std::move(make_compressor))
    , out_(out)
    , err_(err)
{
}

int CLI::run(std::span<const char* const> args)
{
    ::CLI::App app("Huffman-coding compressor", "compressor");
    app.set_version_flag("--version", COMPRESSOR_VERSION);
    app.require_subcommand(1);

    Settings encode_settings;
    Settings decode_settings;
    auto* encode = add_command(app, "encode", "Compress a file", encode_settings);
    add_command(app, "decode", "Decompress a file", decode_settings);

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
        const auto compressor = make_compressor_(encode_settings.threads);
        if (compressor->encode(encode_settings.input, encode_settings.output))
            return EXIT_SUCCESS;
        err_ << "Encoding " << display_name(encode_settings.input, "standard input") << " failed\n";
        return EXIT_FAILURE;
    }

    const auto compressor = make_compressor_(decode_settings.threads);
    if (compressor->decode(decode_settings.input, decode_settings.output))
        return EXIT_SUCCESS;
    err_ << "Decoding " << display_name(decode_settings.input, "standard input") << " failed\n";
    return EXIT_FAILURE;
}

} // namespace compressor
