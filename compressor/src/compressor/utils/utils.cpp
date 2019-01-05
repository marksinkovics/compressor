#include <compressor/utils/utils.h>

#include <iostream>
#include <sstream>

namespace compressor::utils
{

void handle_illegal_position(const std::size_t& pos) noexcept
{
    std::cerr << "Attempted to access an illegal position (" << pos << ")." << std::endl;
}

} // compressor::utils
