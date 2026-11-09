project(compressor_benchmark)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/bitset/Creation.cpp"
    "${SRC_DIR}/bitset/Access.cpp"
    "${SRC_DIR}/bitset/Copying.cpp"
)

add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${INCLUDE_FILES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        compressor
        benchmark
        benchmark_main
        Boost::headers
        compressor_warnings
)
