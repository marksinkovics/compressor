#include <iostream>
#include <compressor/compressor.h>

int main(int argc, char** argv) {
	Compressor compressor;
	std::cout << "Compressor value is " << compressor.value() << std::endl;
	return 0;
}
