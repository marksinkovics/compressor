#include "cli.h"

#include <iostream>

#include <argparser/argparser.h>
#include <compressor/compressor.h>
#include <compressor/decompressor.h>

namespace compressor
{
    
CLI::CLI(int argc, char *argv[])
    : parser_(std::make_shared<argparser::Argparser>(argc, argv))
{
    parser_->add_argument("encode", "Compress the given file.", false);
    parser_->add_argument("decode", "Decompress the given file.", false);
    parser_->add_argument("--input", "Input file path", std::string(""));
    parser_->add_argument("--output", "Output file path", std::string(""));
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
        compressor::Compressor compressor(input_file, output_file);
        compressor.encode();
    }
    else if (parser_->has_argument("decode"))
    {
        std::cout << "Decode\n";
        compressor::Decompressor decompressor(input_file, output_file);
        decompressor.decode();
    }
}

} // compressor
