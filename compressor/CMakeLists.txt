project(compressor)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(COMPRESSOR_INCLUDE_DIR "${INCLUDE_DIR}/compressor")
set(INCLUDE_FILES
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/node.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/tree.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/binary/nodeiterator.h"
    "${COMPRESSOR_INCLUDE_DIR}/tree/symbolnode.h"
    "${COMPRESSOR_INCLUDE_DIR}/archive/archive.h"
    "${COMPRESSOR_INCLUDE_DIR}/archive/index.h"
    "${COMPRESSOR_INCLUDE_DIR}/archive/metadata.h"
    "${COMPRESSOR_INCLUDE_DIR}/bwt/bwt.h"
    "${COMPRESSOR_INCLUDE_DIR}/checksum/crc32.h"
    "${COMPRESSOR_INCLUDE_DIR}/data/data.h"
    "${COMPRESSOR_INCLUDE_DIR}/deflate/deflate.h"
    "${COMPRESSOR_INCLUDE_DIR}/deflate/inflate.h"
    "${COMPRESSOR_INCLUDE_DIR}/deflate/zip.h"
    "${COMPRESSOR_INCLUDE_DIR}/lz77/lz77.h"
    "${COMPRESSOR_INCLUDE_DIR}/cm/cm.h"
    "${COMPRESSOR_INCLUDE_DIR}/lzr/lzr.h"
    "${COMPRESSOR_INCLUDE_DIR}/lzt/lzt.h"
    "${COMPRESSOR_INCLUDE_DIR}/range/range.h"
    "${COMPRESSOR_INCLUDE_DIR}/repair/repair.h"
    "${COMPRESSOR_INCLUDE_DIR}/compressor.h"
    "${COMPRESSOR_INCLUDE_DIR}/engine/engine.h"
    "${COMPRESSOR_INCLUDE_DIR}/dictionary/dictionary.h"
    "${COMPRESSOR_INCLUDE_DIR}/entropy/rans.h"
    "${COMPRESSOR_INCLUDE_DIR}/entropy/tans.h"
    "${COMPRESSOR_INCLUDE_DIR}/error.h"
    "${COMPRESSOR_INCLUDE_DIR}/fmindex/fmindex.h"
    "${COMPRESSOR_INCLUDE_DIR}/gpu/gpu.h"
    "${COMPRESSOR_INCLUDE_DIR}/huffman/huffman.h"
    "${COMPRESSOR_INCLUDE_DIR}/search/search.h"
    "${COMPRESSOR_INCLUDE_DIR}/stats/stats.h"
    "${COMPRESSOR_INCLUDE_DIR}/stream/encoder.h"
    "${COMPRESSOR_INCLUDE_DIR}/stream/decoder.h"
    "${COMPRESSOR_INCLUDE_DIR}/stream/stdio.h"
    "${COMPRESSOR_INCLUDE_DIR}/thread/pool.h"
    "${COMPRESSOR_INCLUDE_DIR}/transform/transform.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(COMPRESSOR_SRC_DIR "${SRC_DIR}/compressor")
set(SRC_FILES
    "${COMPRESSOR_SRC_DIR}/tree/binary/node.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/binary/tree.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/binary/nodeiterator.cpp"
    "${COMPRESSOR_SRC_DIR}/tree/symbolnode.cpp"
    "${COMPRESSOR_SRC_DIR}/deflate/deflate.cpp"
    "${COMPRESSOR_SRC_DIR}/deflate/inflate.cpp"
    "${COMPRESSOR_SRC_DIR}/deflate/zip.cpp"
    "${COMPRESSOR_SRC_DIR}/lz77/lz77.cpp"
    "${COMPRESSOR_SRC_DIR}/cm/cm.cpp"
    "${COMPRESSOR_SRC_DIR}/lzr/lzr.cpp"
    "${COMPRESSOR_SRC_DIR}/lzt/lzt.cpp"
    "${COMPRESSOR_SRC_DIR}/range/range.cpp"
    "${COMPRESSOR_SRC_DIR}/repair/repair.cpp"
    "${COMPRESSOR_SRC_DIR}/search/search.cpp"
    "${COMPRESSOR_SRC_DIR}/stats/stats.cpp"
    "${COMPRESSOR_SRC_DIR}/archive/archive.cpp"
    "${COMPRESSOR_SRC_DIR}/archive/index.cpp"
    "${COMPRESSOR_SRC_DIR}/archive/metadata.cpp"
    "${COMPRESSOR_SRC_DIR}/bwt/bwt.cpp"
    "${COMPRESSOR_SRC_DIR}/checksum/crc32.cpp"
    "${COMPRESSOR_SRC_DIR}/data/data.cpp"
    "${COMPRESSOR_SRC_DIR}/compressor.cpp"
    "${COMPRESSOR_SRC_DIR}/engine/engine.cpp"
    "${COMPRESSOR_SRC_DIR}/dictionary/dictionary.cpp"
    "${COMPRESSOR_SRC_DIR}/entropy/rans.cpp"
    "${COMPRESSOR_SRC_DIR}/entropy/tans.cpp"
    "${COMPRESSOR_SRC_DIR}/error.cpp"
    "${COMPRESSOR_SRC_DIR}/fmindex/fmindex.cpp"
    "${COMPRESSOR_SRC_DIR}/gpu/gpu.cpp"
    "${COMPRESSOR_SRC_DIR}/huffman/huffman.cpp"
    "${COMPRESSOR_SRC_DIR}/stream/encoder.cpp"
    "${COMPRESSOR_SRC_DIR}/stream/decoder.cpp"
    "${COMPRESSOR_SRC_DIR}/stream/stdio.cpp"
    "${COMPRESSOR_SRC_DIR}/thread/pool.cpp"
    "${COMPRESSOR_SRC_DIR}/transform/transform.cpp"
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

# The kernels, compiled to a metallib and embedded as bytes, so nothing has to be installed beside
# the binaries.
if(COMPRESSOR_METAL)
    enable_language(OBJCXX)
    set(METAL_SOURCE "${COMPRESSOR_SRC_DIR}/gpu/kernels.metal")
    set(METAL_AIR "${CMAKE_CURRENT_BINARY_DIR}/kernels.air")
    set(METAL_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/kernels.metallib")
    set(METAL_EMBEDDED "${CMAKE_CURRENT_BINARY_DIR}/metallib.cpp")
    add_custom_command(OUTPUT "${METAL_AIR}"
        COMMAND xcrun -sdk macosx metal -std=metal3.1 -O3 -Werror -c "${METAL_SOURCE}"
                -o "${METAL_AIR}"
        DEPENDS "${METAL_SOURCE}" VERBATIM)
    add_custom_command(OUTPUT "${METAL_LIBRARY}"
        COMMAND xcrun -sdk macosx metallib "${METAL_AIR}" -o "${METAL_LIBRARY}"
        DEPENDS "${METAL_AIR}" VERBATIM)
    add_custom_command(OUTPUT "${METAL_EMBEDDED}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${METAL_LIBRARY} -DOUTPUT=${METAL_EMBEDDED}
                -DSYMBOL=compressor_metallib -P "${CMAKE_SOURCE_DIR}/cmake/EmbedFile.cmake"
        DEPENDS "${METAL_LIBRARY}" "${CMAKE_SOURCE_DIR}/cmake/EmbedFile.cmake" VERBATIM)
    target_sources(${PROJECT_NAME} PRIVATE "${COMPRESSOR_SRC_DIR}/gpu/gpu.mm" "${METAL_EMBEDDED}"
                                           "${METAL_SOURCE}")
    set_source_files_properties("${COMPRESSOR_SRC_DIR}/gpu/gpu.mm" PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(${PROJECT_NAME} PRIVATE COMPRESSOR_METAL)
    target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Metal" "-framework Foundation")
endif()

find_package(Threads REQUIRED)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        instrumentor
        Threads::Threads
)

add_subdirectory(test)
