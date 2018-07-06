#include <compressor/compressor.h>

Compressor::Compressor() {
	this->value_ = 42;
}

int Compressor::value() const {
	return this->value_;
}
