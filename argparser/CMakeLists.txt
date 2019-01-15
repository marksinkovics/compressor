project(argparser)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

SET(ARGPARSER_INCLUDE_DIR "${INCLUDE_DIR}/argparser")

set(INCLUDE_FILES
    "${ARGPARSER_INCLUDE_DIR}/argparser.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
SET(ARGPARSER_SRC_DIR "${SRC_DIR}/argparser")
set(SRC_FILES
    "${ARGPARSER_SRC_DIR}/argparser.cpp"
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

add_subdirectory(test)
