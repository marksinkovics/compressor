#include <compressor/cli.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <CLI/CLI.hpp>

#include <compressor/archive/archive.h>
#include <compressor/compressor.h>
#include <compressor/lz77/lz77.h>

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
    unsigned level = default_level;
    bool search_index = false;
    bool details = false;
    StatsOptions stats;
    std::string graph;
    SearchOptions search;
    std::vector<std::string> paths;
    std::string format = "ingot";
    bool count = false;
    bool names = false;
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
    Settings grep_settings;
    Settings search_settings;
    auto* encode = add_command(app, "encode", "Compress a file or a folder", true, encode_settings);
    encode
        ->add_option("-L,--level", encode_settings.level,
                     "1: Huffman only, fastest; 2-9: also LZ77 + Huffman, searching longer "
                     "for matches the higher (default 6)")
        ->check(::CLI::Range(min_level, max_level));
    encode->add_flag("--search-index", encode_settings.search_index,
                     "For a folder archive: add an FM-index, so search finds patterns without "
                     "decoding (slower to encode, larger)");
    encode
        ->add_option("-f,--format", encode_settings.format,
                     "ingot (default), gzip (a file) or zip (a folder)")
        ->check(::CLI::IsMember({"ingot", "gzip", "zip"}));
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

    auto* grep = app.add_subcommand(
        "grep", "Print the lines that contain a fixed string, in an ingot file or a folder "
                "archive's files. Exit code 0: a line matched, 1: none did, 2: an error");
    grep->add_option("pattern", grep_settings.search.pattern, "Fixed string to look for")
        ->required();
    grep->add_option("input", grep_settings.input, "Input file; -: standard input")
        ->required()
        ->check(existing_input(false));
    grep->add_option("path", grep_settings.paths,
                     "Search only these files or directories of a folder archive");
    auto* count = grep->add_flag("-c,--count", grep_settings.count,
                                 "Print the number of matching lines per file");
    grep->add_flag("-l,--files-with-matches", grep_settings.names,
                   "Print the names of the files with a match")
        ->excludes(count);
    grep->add_flag("-n,--line-number", "Accepted for grep's sake; line numbers are always shown");
    grep->add_flag("-b,--byte-offset", grep_settings.search.byte_offsets,
                   "Print where each matching line starts in its file");
    add_threads(*grep, grep_settings);

    auto* search = app.add_subcommand(
        "search", "Find a fixed string in a folder archive through its search index, without "
                  "decoding; prints path:offset. Exit code 0: found, 1: not, 2: an error");
    search->add_option("pattern", search_settings.search.pattern, "Fixed string, 1 to 4096 bytes")
        ->required();
    search->add_option("-i,--input,archive", search_settings.input, "Folder archive file")
        ->required()
        ->check(::CLI::ExistingFile);
    search->add_flag("-c,--count", search_settings.count, "Print each file's number of matches");
    add_threads(*search, search_settings);

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
        const auto compressor = make_compressor_({.threads = encode_settings.threads,
                                                  .level = encode_settings.level,
                                                  .search_index = encode_settings.search_index});
        const bool encoded =
            encode_settings.format == "ingot"
                ? compressor->encode(encode_settings.input, encode_settings.output)
                : compressor->encode_as(encode_settings.input, encode_settings.output,
                                        encode_settings.format == "gzip" ? OutputFormat::gzip
                                                                         : OutputFormat::zip);
        return finish(encoded, "Encoding " + input_name(encode_settings));
    }
    if (decode->parsed())
    {
        const auto compressor =
            make_compressor_({.threads = decode_settings.threads, .level = decode_settings.level});
        return finish(compressor->decode(decode_settings.input, decode_settings.output),
                      "Decoding " + input_name(decode_settings));
    }
    if (list->parsed())
    {
        const auto compressor = make_compressor_({.threads = 1, .level = default_level});
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
        const auto compressor =
            make_compressor_({.threads = settings.threads, .level = settings.level});
        return finish(compressor->extract(settings.input, settings.path, output),
                      "Extracting " + settings.path + " from " + settings.input);
    }
    if (search->parsed())
    {
        const auto compressor =
            make_compressor_({.threads = search_settings.threads, .level = default_level});
        switch (compressor->search(search_settings.input, search_settings.search.pattern,
                                   search_settings.count))
        {
        case SearchResult::matched:
            return EXIT_SUCCESS;
        case SearchResult::not_matched:
            return 1;
        case SearchResult::failed:
            break;
        }
        err_ << "Searching " << search_settings.input << " failed\n";
        return 2;
    }
    if (grep->parsed())
    {
        if (grep_settings.count)
        {
            grep_settings.search.output = SearchOutput::count;
        }
        if (grep_settings.names)
        {
            grep_settings.search.output = SearchOutput::names;
        }
        const auto compressor =
            make_compressor_({.threads = grep_settings.threads, .level = grep_settings.level});
        switch (compressor->grep(grep_settings.input, grep_settings.search, grep_settings.paths))
        {
        case SearchResult::matched:
            return EXIT_SUCCESS;
        case SearchResult::not_matched:
            return 1;
        case SearchResult::failed:
            break;
        }
        err_ << "Searching " << input_name(grep_settings) << " failed\n";
        return 2;
    }
    if (stats->parsed())
    {
        stats_settings.stats.graph = stats_settings.graph;
        const auto compressor = make_compressor_({.threads = 1, .level = default_level});
        return finish(compressor->stats(stats_settings.input, stats_settings.stats),
                      "Analysing " + stats_settings.input);
    }
    const auto compressor =
        make_compressor_({.threads = verify_settings.threads, .level = verify_settings.level});
    return finish(compressor->verify(verify_settings.input),
                  "Verifying " + input_name(verify_settings));
}

} // namespace compressor
