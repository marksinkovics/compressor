project(compressor_benchmark)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/bitvector/BitVectorBenchmark.cpp"
)

add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${INCLUDE_FILES}
)

target_link_libraries(${PROJECT_NAME}
    compressor
    GBenchmark::GBenchmark
    GBenchmark::Main
)

