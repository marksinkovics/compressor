project(compressor_test)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES
    "${INCLUDE_DIR}/compressor/test/mock/compressor.h"
    "${INCLUDE_DIR}/compressor/test/mock/engine/engine.h"
    "${INCLUDE_DIR}/compressor/test/mock/stream/encoder.h"
    "${INCLUDE_DIR}/compressor/test/mock/stream/decoder.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/archive/archive.cpp"
    "${SRC_DIR}/archive/index.cpp"
    "${SRC_DIR}/bwt/bwt.cpp"
    "${SRC_DIR}/checksum/crc32.cpp"
    "${SRC_DIR}/cm/cm.cpp"
    "${SRC_DIR}/compressor/compressor.cpp"
    "${SRC_DIR}/engine/engine.cpp"
    "${SRC_DIR}/entropy/rans.cpp"
    "${SRC_DIR}/entropy/tans.cpp"
    "${SRC_DIR}/fmindex/fmindex.cpp"
    "${SRC_DIR}/huffman/huffman.cpp"
    "${SRC_DIR}/lzr/lzr.cpp"
    "${SRC_DIR}/lzt/lzt.cpp"
    "${SRC_DIR}/range/range.cpp"
    "${SRC_DIR}/repair/repair.cpp"
    "${SRC_DIR}/tree/binary/node.cpp"
    "${SRC_DIR}/tree/binary/tree.cpp"
    "${SRC_DIR}/tree/binary/nodeiterator.cpp"
    "${SRC_DIR}/data/data.cpp"
    "${SRC_DIR}/dictionary/dictionary.cpp"
    "${SRC_DIR}/deflate/deflate.cpp"
    "${SRC_DIR}/deflate/gunzip.cpp"
    "${SRC_DIR}/deflate/inflate.cpp"
    "${SRC_DIR}/deflate/zip.cpp"
    "${SRC_DIR}/roundtrip/roundtrip.cpp"
    "${SRC_DIR}/search/search.cpp"
    "${SRC_DIR}/stats/stats.cpp"
    "${SRC_DIR}/stream/stream.cpp"
    "${SRC_DIR}/stream/stdio.cpp"
    "${SRC_DIR}/thread/pool.cpp"
    "${SRC_DIR}/transform/transform.cpp"
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

include(PackageTest)
package_add_test(
    NAME
        ${PROJECT_NAME}
    HEADERS
        ${INCLUDE_DIR}
    SOURCES
        ${INCLUDE_FILES}
        ${SRC_FILES}
    LIBRARIES
        compressor
        ZLIB::ZLIB
)

foreach(target ${PROJECT_NAME} ${PROJECT_NAME}_app)
    target_compile_definitions(${target}
        PRIVATE
            COMPRESSOR_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/compressor_example/samples/raw"
    )
endforeach()
