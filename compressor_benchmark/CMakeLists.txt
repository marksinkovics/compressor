project(compressor_benchmark)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/main.cpp"
    "${SRC_DIR}/bitset/Creation.cpp"
    "${SRC_DIR}/bitset/Access.cpp"
    "${SRC_DIR}/codec/codec.cpp"
    "${SRC_DIR}/codec/codec.h"
)

add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${INCLUDE_FILES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        COMPRESSOR_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/compressor_example/samples/raw"
        COMPRESSOR_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        compressor
        benchmark::benchmark
        Boost::dynamic_bitset
        ZLIB::ZLIB
)
