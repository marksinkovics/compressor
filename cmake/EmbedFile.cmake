# cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -P EmbedFile.cmake
# Writes a C++ source defining `extern "C" const unsigned char SYMBOL[]` and `SYMBOL_size`.
file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
file(WRITE "${OUTPUT}" "// Generated from ${INPUT}; do not edit.\n"
    "extern \"C\" const unsigned char ${SYMBOL}[] = {${bytes}};\n"
    "extern \"C\" const unsigned long ${SYMBOL}_size = ${size};\n")
