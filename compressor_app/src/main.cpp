#include <iostream>
#include <fstream>

#include <compressor/engine.h>
#include <compressor/compressor.h>
#include <compressor/decompressor.h>
#include <compressor/bitset/bitset.h>

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <boost/timer.hpp>

#include <algorithm>
#include <limits>

#include <argparser/argparser.h>

std::string lorem() {
    return std::string("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.");
}

std::string dummy() {
    return std::string("go go gophers");
}

void compress()
{
    auto input_file = "/tmp/original.txt";
    auto output_file = "/tmp/encoded.txt";
    
    std::ifstream istream;
    istream.open(input_file, std::ios::in | std::ios::binary);
    if (istream.fail()) {
        std::cerr << "Cannot open file: " << input_file << '\n';
        return;
    }
    
    compressor::Data data;
    istream >> data;
    istream.close();
    
    compressor::Engine compressor(data);
    auto encoded_data = compressor.encode();
    
    std::ofstream ostream;
    ostream.open(output_file, std::ios::out | std::ios::binary);
    if (ostream.fail()) {
        std::cerr << "Cannot open file: " << output_file << '\n';
        return;
    }
    ostream << encoded_data;
    ostream.close();
}

void decompress()
{
    auto input_file = "/tmp/encoded.txt";
    auto output_file = "/tmp/decoded.txt";
    
    std::ifstream istream;
    istream.open(input_file, std::ios::in | std::ios::binary);
    if (istream.fail()) {
        std::cerr << "Cannot open file: " << input_file << '\n';
        return;
    }
    
    compressor::EncodedData encoded_data;
    istream >> encoded_data;
    istream.close();
    
    compressor::Engine compressor(encoded_data);
    auto data = compressor.decode();
    
    std::ofstream ostream;
    ostream.open(output_file, std::ios::out | std::ios::binary);
    if (ostream.fail()) {
        std::cerr << "Cannot open file: " << output_file << '\n';
        return;
    }
    ostream << data;
    ostream.close();
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[]) {
    
    argparser::Argparser parser(argc, argv);
    parser.add_argument("encode", false, "Compress the given file.");
    parser.add_argument("decode", false, "Decompress the given file.");
    parser.add_argument("--input", std::string(), "Input file path");
    parser.add_argument("--output", std::string(), "Output file path");

    auto options = parser.parse();
    std::string input_file, output_file;
    if (parser.has_argument("--input") && std::any_cast<std::string>(options["--input"]).size() > 0)
    {
        input_file = std::any_cast<std::string>(options["--input"]);
        std::cout << "Input file: "<< input_file << '\n';
    }

    if (parser.has_argument("--output") && std::any_cast<std::string>(options["--output"]).size() > 0)
    {
        output_file = std::any_cast<std::string>(options["--output"]);
        std::cout << "Output file: "<< output_file << '\n';
    }

    if (parser.has_argument("encode") && std::any_cast<bool>(options["encode"]))
    {
        std::cout << "Encode\n";
        compressor::Compressor compressor(input_file, output_file);
        compressor.encode();
    }
    else if (parser.has_argument("decode") && std::any_cast<bool>(options["decode"]))
    {
        std::cout << "Decode\n";
        compressor::Decompressor decompressor(input_file, output_file);
        decompressor.decode();
    }
    
//    compressor::Compressor compressor("/tmp/original.txt", "/tmp/encoded.txt");
//    compressor.encode();
//
//    compressor::Decompressor decompressor("/tmp/encoded.txt", "/tmp/decoded.txt");
//    decompressor.decode();
    
//    compress();
//    decompress();
    
	return 0;
}
