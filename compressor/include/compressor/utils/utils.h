#ifndef Utils_hpp
#define Utils_hpp

#include <cstddef>
#include <string>
#include <sstream>

namespace compressor::utils
{

void handle_illegal_position(const std::size_t& pos) noexcept;

template<typename T>
std::string block_to_bit_string(T value, T length = std::numeric_limits<T>::digits)
{
    if (length > std::numeric_limits<T>::digits)
    {
        handle_illegal_position(length);
        return "";
    }

    std::stringstream stream;

    for (std::size_t bit_index = 0; bit_index < length; bit_index++)
    {
        stream << ((value >> (length - bit_index - 1)) & 0x1);
    }

    return stream.str();
}

inline constexpr std::size_t calc_num_blocks(std::size_t num_bits, std::size_t bits_per_block) noexcept
{
    return num_bits / bits_per_block + static_cast<std::size_t>(num_bits % bits_per_block != 0);
}

} // compressor::utils

#endif //Utils_hpp


