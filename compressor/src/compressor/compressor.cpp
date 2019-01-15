#include <compressor/compressor.h>

#include <compressor/data/data.h>
#include <compressor/engine.h>

namespace compressor
{

Compressor::Compressor(const std::string& input_file, const std::string& output_file)
    : input_file_(input_file)
    , output_file_(output_file)
{
    
}
    
Compressor::~Compressor()
{

}

void Compressor::encode()
{
    std::ifstream input_stream;
    input_stream.open(input_file_, std::ios::in | std::ios::binary);
    if (input_stream.fail()) {
        std::cerr << "Cannot open file: " << input_file_ << '\n';
        return;
    }

    compressor::Data data;
    input_stream >> data;
    input_stream.close();
    
    compressor::Engine compressor(data);
    auto encoded_data = compressor.encode();

    std::ofstream output_stream;
    output_stream.open(output_file_, std::ios::out | std::ios::binary);
    if (output_stream.fail()) {
        std::cerr << "Cannot open file: " << output_file_ << '\n';
        return;
    }

    output_stream << encoded_data;
    output_stream.close();
}

}
