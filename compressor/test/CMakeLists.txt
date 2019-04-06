project(compressor_test)

enable_testing()

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES
    "${INCLUDE_DIR}/compressor/test/mock/compressor.h"
    "${INCLUDE_DIR}/compressor/test/mock/engine/engine.h"
    "${INCLUDE_DIR}/compressor/test/mock/task/encoder.h"
    "${INCLUDE_DIR}/compressor/test/mock/task/decoder.h"
    "${INCLUDE_DIR}/compressor/test/mock/data/data.h"
    "${INCLUDE_DIR}/compressor/test/mock/data/reader.h"
    "${INCLUDE_DIR}/compressor/test/mock/data/writer.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/compressor/compressor.cpp"
    "${SRC_DIR}/engine/engine.cpp"
    "${SRC_DIR}/bitset/bitset.cpp"
    "${SRC_DIR}/bitset/bitset_iterator.cpp"
    "${SRC_DIR}/tree/binary/node.cpp"
    "${SRC_DIR}/tree/binary/tree.cpp"
    "${SRC_DIR}/tree/binary/nodeiterator.cpp"
    "${SRC_DIR}/utils/utils.cpp"
    "${SRC_DIR}/data/data.cpp"
)

add_library(${PROJECT_NAME}
    ${INCLUDE_FILES}
    ${SRC_FILES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${Boost_INCLUDE_DIR}
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)

target_link_libraries(${PROJECT_NAME}
    compressor
    GTest::GTest
    GMock::GMock
)
