project(compressor)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(COMPRESSOR_INCLUDE_DIR "${INCLUDE_DIR}/compressor")
set(INCLUDE_FILES
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/node.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/tree.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/nodeiterator.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/symbolnode.h"
    "${COMPRESSOR_INCLUDE_DIR}/checksum/crc32.h"
    "${COMPRESSOR_INCLUDE_DIR}/data/data.h"
    "${COMPRESSOR_INCLUDE_DIR}/compressor.h"
    "${COMPRESSOR_INCLUDE_DIR}/engine/engine.h"
    "${COMPRESSOR_INCLUDE_DIR}/error.h"
    "${COMPRESSOR_INCLUDE_DIR}/huffman/huffman.h"
    "${COMPRESSOR_INCLUDE_DIR}/stream/encoder.h"
    "${COMPRESSOR_INCLUDE_DIR}/stream/decoder.h"
    "${COMPRESSOR_INCLUDE_DIR}/stream/stdio.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(COMPRESSOR_SRC_DIR "${SRC_DIR}/compressor")
set(SRC_FILES
    "${COMPRESSOR_SRC_DIR}/tree/binary/node.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/binary/tree.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/binary/nodeiterator.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/symbolnode.cpp"
    "${COMPRESSOR_SRC_DIR}/checksum/crc32.cpp"
    "${COMPRESSOR_SRC_DIR}/data/data.cpp"
    "${COMPRESSOR_SRC_DIR}/compressor.cpp"
    "${COMPRESSOR_SRC_DIR}/engine/engine.cpp"
    "${COMPRESSOR_SRC_DIR}/error.cpp"
    "${COMPRESSOR_SRC_DIR}/huffman/huffman.cpp"
    "${COMPRESSOR_SRC_DIR}/stream/encoder.cpp"
    "${COMPRESSOR_SRC_DIR}/stream/decoder.cpp"
    "${COMPRESSOR_SRC_DIR}/stream/stdio.cpp"
)

add_library(${PROJECT_NAME} ${INCLUDE_FILES} ${SRC_FILES} )

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        instrumentor
)

add_subdirectory(test)
