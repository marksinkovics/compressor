#include <compressor/cli.h>

#include <iostream>

#include <argparser/argparser.h>
#include <compressor/compressor.h>

namespace compressor
{

CLI::CLI(std::unique_ptr<argparser::IArgparser>&& parser,
         std::unique_ptr<compressor::ICompressor>&& compressor)
: parser_(std::move(parser))
, compressor_(std::move(compressor))
{
    parser_->add_option(argparser::CreateArg("encode", "", "Compress the given file.", false));
    parser_->add_option(argparser::CreateArg("decode", "", "Decompress the given file.", false));
    parser_->add_option(argparser::CreateArg("--input", "", "Input file path", std::string("")));
    parser_->add_option(argparser::CreateArg("--output", "", "Output file path", std::string("")));
    options_ = parser_->parse();
}

void CLI::run()
{
    std::string input_file, output_file;
    if (parser_->has_argument("input"))
    {
        input_file = std::dynamic_pointer_cast<argparser::Arg<std::string>>(options_["input"])->value();
        std::cout << "Input file: "<< input_file << '\n';
    }

    if (parser_->has_argument("output"))
    {
        output_file = std::dynamic_pointer_cast<argparser::Arg<std::string>>(options_["output"])->value();
        std::cout << "Output file: "<< output_file << '\n';
    }

    if (parser_->has_argument("encode"))
    {
        std::cout << "Encode\n";
        EncoderTask task(input_file, output_file);
        compressor_->encode(task);
    }
    else if (parser_->has_argument("decode"))
    {
        std::cout << "Decode\n";
        DecoderTask task(input_file, output_file);
        compressor_->decode(task);
    }
}

} // compressor
