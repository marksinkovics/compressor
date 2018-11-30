#include <iostream>
#include <fstream>

#include <compressor/compressor.h>
#include <bitvector/bitvector.h>

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <boost/timer.hpp>

#include <algorithm>
#include <limits>


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
    
    Compressor::Data data;
    istream >> data;
    istream.close();
    
    Compressor::Compressor compressor(data);
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
    
    Compressor::EncodedData encoded_data;
    istream >> encoded_data;
    istream.close();
    
    Compressor::Compressor compressor(encoded_data);
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

int main(int argc, char** argv) {
    
    compress();
    decompress();
    
	return 0;
}
