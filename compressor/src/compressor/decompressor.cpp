#include <compressor/decompressor.h>

Decompressor::Decompressor() {
    this->value_ = 42;
}

int Decompressor::value() const {
    return this->value_;
}
