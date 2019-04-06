project(compressor_cli)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

SET(COMPRESSOR_INCLUDE_DIR "${INCLUDE_DIR}/compressor")

set(INCLUDE_FILES
    "${COMPRESSOR_INCLUDE_DIR}/cli.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
SET(COMPRESSOR_SRC_DIR "${SRC_DIR}/compressor")
set(SRC_FILES
    "${COMPRESSOR_SRC_DIR}/cli.cpp"
)

add_library(${PROJECT_NAME} ${INCLUDE_FILES} ${SRC_FILES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)

target_link_libraries(${PROJECT_NAME}
    compressor
    argparser
)

add_subdirectory(test)
