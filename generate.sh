#!/bin/bash

echo "Generate Ninja - Debug"
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DCOMPRESSOR_WERROR=ON -G Ninja

echo "Generate Ninja - Release"
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCOMPRESSOR_WERROR=ON -G Ninja

echo "Generate Xcode"
cmake -S . -B build/xcode -DCOMPRESSOR_WERROR=ON -G Xcode
