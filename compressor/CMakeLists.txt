project(compressor)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

SET(COMPRESSOR_INCLUDE_DIR "${INCLUDE_DIR}/compressor")

set(INCLUDE_FILES
    "${COMPRESSOR_INCLUDE_DIR}/utils/utils.h"
    "${INCLUDE_DIR}/tree/binary/node.h"
    "${INCLUDE_DIR}/tree/binary/tree.h"
    "${INCLUDE_DIR}/tree/binary/nodeiterator.h"
    "${INCLUDE_DIR}/tree/symbolnode.h"
    "${INCLUDE_DIR}/bitvector/bitvector.h"
    "${INCLUDE_DIR}/bitvector/bitvectoriterator.h"
    "${COMPRESSOR_INCLUDE_DIR}/data/data.h"
    "${COMPRESSOR_INCLUDE_DIR}/compressor.h"
    "${COMPRESSOR_INCLUDE_DIR}/decompressor.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
SET(COMPRESSOR_SRC_DIR "${SRC_DIR}/compressor")
set(SRC_FILES
    "${COMPRESSOR_SRC_DIR}/utils/utils.cpp"
    "${SRC_DIR}/tree/binary/node.cpp"
    "${SRC_DIR}/tree/binary/tree.cpp"
    "${SRC_DIR}/tree/binary/nodeiterator.cpp"
    "${SRC_DIR}/tree/symbolnode.cpp"
    "${SRC_DIR}/bitvector/bitvector.cpp"
    "${SRC_DIR}/bitvector/bitvectoriterator.cpp"
    "${COMPRESSOR_SRC_DIR}/data/data.cpp"
    "${COMPRESSOR_SRC_DIR}/compressor.cpp"
    "${COMPRESSOR_SRC_DIR}/decompressor.cpp"
)

add_library(${PROJECT_NAME} ${SRC_FILES} ${INCLUDE_FILES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)
