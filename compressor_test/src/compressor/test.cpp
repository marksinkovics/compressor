#include <vector>

#include <gtest/gtest.h>
#include <compressor/compressor.h>

TEST(Arithmetic, Mod)
{
    std::vector<uint8_t> numbers(10);
    std::generate(numbers.begin(), numbers.end(), [n = 1] () mutable { return n++; });
    for (uint8_t v : numbers) {
        int a = v % 8;
        int b = v & 7;
        EXPECT_EQ(a, b);
    }
}
