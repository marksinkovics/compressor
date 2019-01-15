#include <compressor/decompressor.h>

#include <compressor/data/data.h>
#include <compressor/engine.h>

namespace compressor
{
    
Decompressor::Decompressor(const std::string& input_file, const std::string& output_file)
    : input_file_(input_file)
    , output_file_(output_file)
{
    
}

Decompressor::~Decompressor()
{

}

void Decompressor::decode()
{
    std::ifstream input_stream;
    input_stream.open(input_file_, std::ios::in | std::ios::binary);
    if (input_stream.fail()) {
        std::cerr << "Cannot open file: " << input_file_ << '\n';
        return;
    }

    compressor::EncodedData encoded_data;
    input_stream >> encoded_data;
    input_stream.close();
    
    compressor::Engine compressor(encoded_data);
    auto data = compressor.decode();
    
    std::ofstream output_stream;
    output_stream.open(output_file_, std::ios::out | std::ios::binary);
    if (output_stream.fail()) {
        std::cerr << "Cannot open file: " << output_file_ << '\n';
        return;
    }
    
    output_stream << data;
    output_stream.close();
}

} // compressor
