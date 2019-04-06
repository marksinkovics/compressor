project(compressor)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

SET(COMPRESSOR_INCLUDE_DIR "${INCLUDE_DIR}/compressor")

set(INCLUDE_FILES
    "${COMPRESSOR_INCLUDE_DIR}/utils/utils.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/node.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/tree.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/nodeiterator.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/symbolnode.h"
    "${COMPRESSOR_INCLUDE_DIR}/bitset/bitset.h"
    "${COMPRESSOR_INCLUDE_DIR}/bitset/bitset_iterator.h"
    "${COMPRESSOR_INCLUDE_DIR}/data/data.h"
    "${COMPRESSOR_INCLUDE_DIR}/data/reader.h"
    "${COMPRESSOR_INCLUDE_DIR}/data/writer.h"
    "${COMPRESSOR_INCLUDE_DIR}/compressor.h"
    "${COMPRESSOR_INCLUDE_DIR}/engine/engine.h"
    "${COMPRESSOR_INCLUDE_DIR}/task/coder.h"
    "${COMPRESSOR_INCLUDE_DIR}/task/encoder.h"
    "${COMPRESSOR_INCLUDE_DIR}/task/decoder.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
SET(COMPRESSOR_SRC_DIR "${SRC_DIR}/compressor")
set(SRC_FILES
    "${COMPRESSOR_SRC_DIR}/utils/utils.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/binary/node.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/binary/tree.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/binary/nodeiterator.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/symbolnode.cpp"
    "${COMPRESSOR_SRC_DIR}/bitset/bitset.cpp"
    "${COMPRESSOR_SRC_DIR}/bitset/bitset_iterator.cpp"
    "${COMPRESSOR_SRC_DIR}/data/data.cpp"
    "${COMPRESSOR_SRC_DIR}/data/reader.cpp"
    "${COMPRESSOR_SRC_DIR}/data/writer.cpp"
    "${COMPRESSOR_SRC_DIR}/compressor.cpp"
    "${COMPRESSOR_SRC_DIR}/engine/engine.cpp"
    "${COMPRESSOR_SRC_DIR}/task/coder.cpp"
    "${COMPRESSOR_SRC_DIR}/task/encoder.cpp"
    "${COMPRESSOR_SRC_DIR}/task/decoder.cpp"
)

add_library(${PROJECT_NAME} ${INCLUDE_FILES} ${SRC_FILES} )

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)

add_subdirectory(test)

