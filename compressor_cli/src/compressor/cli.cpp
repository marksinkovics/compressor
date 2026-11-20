#include <compressor/cli.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <CLI/CLI.hpp>

#include <compressor/archive/archive.h>
#include <compressor/compressor.h>

namespace compressor
{

namespace
{

struct Settings
{
    std::string input{standard_stream};
    std::string output{standard_stream};
    std::string path; // in an archive
    unsigned threads = 0;
    bool details = false;
    StatsOptions stats;
    std::string graph;
};

std::string display_name(const std::string& path, std::string_view standard_name)
{
    return path == standard_stream ? std::string(standard_name) : path;
}

void add_threads(::CLI::App& command, Settings& settings)
{
    command.add_option("-T,--threads", settings.threads, "Worker threads; 0 or omitted: all cores")
        ->check(::CLI::NonNegativeNumber);
}

// `folders`: the input may also be a directory.
::CLI::Validator existing_input(bool folders)
{
    return {[folders](std::string& path) {
        if (path == standard_stream)
        {
            return std::string{};
        }
        return folders ? ::CLI::ExistingPath(path) : ::CLI::ExistingFile(path);
    }, folders ? "PATH" : "FILE"};
}

::CLI::App* add_command(::CLI::App& app, const std::string& name, const std::string& description,
                        bool folders, Settings& settings)
{
    auto* command = app.add_subcommand(name, description);
    command
        ->add_option("-i,--input", settings.input,
                     folders ? "Input file or folder; - or omitted: standard input"
                             : "Input file; - or omitted: standard input")
        ->check(existing_input(folders));
    command->add_option("-o,--output", settings.output,
                        folders ? "Output file; - or omitted: standard output"
                                : "Output file, or the new directory for a folder archive; - or "
                                  "omitted: standard output");
    add_threads(*command, settings);
    return command;
}

// The archive comes as -i or as the first positional argument.
::CLI::App* add_archive_command(::CLI::App& app, const std::string& name,
                                const std::string& description, Settings& settings)
{
    auto* command = app.add_subcommand(name, description);
    command->add_option("-i,--input,archive", settings.input, "Folder archive file")
        ->required()
        ->check(::CLI::ExistingFile);
    return command;
}

void add_path(::CLI::App& command, Settings& settings)
{
    command.add_option("path", settings.path, "File, directory or symlink in the archive")
        ->required();
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
    Settings list_settings;
    Settings extract_settings;
    Settings cat_settings;
    Settings verify_settings;
    Settings stats_settings;
    auto* encode = add_command(app, "encode", "Compress a file or a folder", true, encode_settings);
    auto* decode =
        add_command(app, "decode", "Decompress a file or a folder archive", false, decode_settings);

    auto* list =
        add_archive_command(app, "list", "List the paths in a folder archive", list_settings);
    list->add_flag("-l,--long", list_settings.details,
                   "Also show types, permissions, sizes and modification times (UTC)");

    auto* extract = add_archive_command(
        app, "extract", "Extract one file, directory or symlink from a folder archive",
        extract_settings);
    add_path(*extract, extract_settings);
    extract_settings.output.clear();
    extract->add_option("-o,--output", extract_settings.output,
                        "Output path; omitted: the entry's name in the current directory; -: "
                        "standard output, for a file");
    add_threads(*extract, extract_settings);

    auto* cat = add_archive_command(app, "cat",
                                    "Write one file from a folder archive to standard "
                                    "output",
                                    cat_settings);
    add_path(*cat, cat_settings);
    add_threads(*cat, cat_settings);

    auto* verify = app.add_subcommand(
        "verify", "Decode a file or folder archive and check every checksum, writing nothing");
    verify
        ->add_option("-i,--input,input", verify_settings.input,
                     "Input file; - or omitted: standard input")
        ->check(existing_input(false));
    add_threads(*verify, verify_settings);

    auto* stats = app.add_subcommand(
        "stats", "Show what a file holds and how well it compresses; an ingot file is decoded");
    stats->add_option("-i,--input,input", stats_settings.input, "Input file")
        ->required()
        ->check(::CLI::ExistingFile);
    stats->add_flag("--blocks", stats_settings.stats.report.blocks,
                    "One line per block of an ingot file");
    stats->add_flag("--histogram", stats_settings.stats.report.histogram,
                    "One line per byte value, most frequent first");
    stats->add_option("--graph", stats_settings.graph,
                      "Write the Huffman code tree as Graphviz to this file; -: standard output, "
                      "instead of the report");

    try
    {
        app.parse(static_cast<int>(args.size()), args.data());
    }
    catch (const ::CLI::ParseError& e)
    {
        return app.exit(e, out_, err_);
    }

    const auto finish = [this](bool done, const std::string& what) {
        if (done)
        {
            return EXIT_SUCCESS;
        }
        err_ << what << " failed\n";
        return EXIT_FAILURE;
    };
    const auto input_name = [](const Settings& settings) {
        return display_name(settings.input, "standard input");
    };

    if (encode->parsed())
    {
        const auto compressor = make_compressor_(encode_settings.threads);
        return finish(compressor->encode(encode_settings.input, encode_settings.output),
                      "Encoding " + input_name(encode_settings));
    }
    if (decode->parsed())
    {
        const auto compressor = make_compressor_(decode_settings.threads);
        return finish(compressor->decode(decode_settings.input, decode_settings.output),
                      "Decoding " + input_name(decode_settings));
    }
    if (list->parsed())
    {
        const auto compressor = make_compressor_(1);
        return finish(compressor->list(list_settings.input, list_settings.details),
                      "Listing " + list_settings.input);
    }
    if (extract->parsed() || cat->parsed())
    {
        Settings& settings = extract->parsed() ? extract_settings : cat_settings;
        std::filesystem::path output =
            cat->parsed() ? std::string{standard_stream} : settings.output;
        if (output.empty())
        {
            output = std::filesystem::path(archive_path(settings.path)).filename();
        }
        const auto compressor = make_compressor_(settings.threads);
        return finish(compressor->extract(settings.input, settings.path, output),
                      "Extracting " + settings.path + " from " + settings.input);
    }
    if (stats->parsed())
    {
        stats_settings.stats.graph = stats_settings.graph;
        const auto compressor = make_compressor_(1);
        return finish(compressor->stats(stats_settings.input, stats_settings.stats),
                      "Analysing " + stats_settings.input);
    }
    const auto compressor = make_compressor_(verify_settings.threads);
    return finish(compressor->verify(verify_settings.input),
                  "Verifying " + input_name(verify_settings));
}

} // namespace compressor
