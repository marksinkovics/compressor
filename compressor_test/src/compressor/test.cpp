#include <gtest/gtest.h>
#include <compressor/compressor.h>

TEST(Example, First) {
	ASSERT_TRUE(1 == 1);
}

TEST(Example, Second) {
	ASSERT_TRUE(2 == 2);
}

TEST(Example, Third) {
	ASSERT_TRUE(3 == 3);
}

TEST(Compressor, Value) {
	Compressor compressor;
	ASSERT_TRUE(42 == compressor.value());
}
