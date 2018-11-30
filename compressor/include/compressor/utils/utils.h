#ifndef Utils_hpp
#define Utils_hpp

#include <cstddef>
#include <string>

class Utils
{
public:
    static std::string byte_to_bit_string(uint8_t value, uint8_t length = 8) noexcept;
    static std::size_t calculate_byte_size(std::size_t bit_size) noexcept;
    static void handle_illegal_position(const std::size_t& pos) noexcept;
};

#endif //Utils_hpp


