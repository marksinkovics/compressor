#include <compressor/utils/utils.h>

#include <iostream>
#include <sstream>

std::string Utils::byte_to_bit_string(uint8_t byte, uint8_t length) noexcept
{
    if (length > 8)
    {
        handle_illegal_position(length);
        return "";
    }
    
    std::stringstream stream;
    for (std::size_t bit_index = 0; bit_index < length; bit_index++)
    {
        stream << ((byte >> (length - bit_index - 1)) & 0x1);
    }
    return stream.str();
}

std::size_t Utils::calculate_byte_size(std::size_t bit_size) noexcept
{
    return (bit_size >> 3) + ((bit_size & 7) ? 1 : 0);
}

void Utils::handle_illegal_position(const std::size_t& pos) noexcept
{
    std::cerr << "Attempted to access an illegal position (" << pos << ")." << std::endl;
}
