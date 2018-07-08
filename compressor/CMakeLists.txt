project(compressor)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES
    ${INCLUDE_DIR}/BitVector.hpp
    ${INCLUDE_DIR}/compressor/compressor.h
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/compressor.cpp"
    "${SRC_DIR}/BitVector.cpp"
)

add_library(${PROJECT_NAME} ${SRC_FILES} ${INCLUDE_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)
