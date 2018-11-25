project(compressor)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

set(INCLUDE_FILES
    ${INCLUDE_DIR}/bitvector/BitVector.hpp
    ${INCLUDE_DIR}/bitvector/BitVectorIterator.hpp
    ${INCLUDE_DIR}/compressor/compressor.h
    ${INCLUDE_DIR}/compressor/decompressor.h
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/bitvector/BitVector.cpp"
    "${SRC_DIR}/bitvector/BitVectorIterator.cpp"
    "${SRC_DIR}/compressor/compressor.cpp"
    "${SRC_DIR}/compressor/decompressor.cpp"
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
