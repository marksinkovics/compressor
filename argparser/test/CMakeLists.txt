project(argparser_test)

enable_testing()

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES
    "${INCLUDE_DIR}/argparser/test/mock/argparser.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/valueparser.cpp"
    "${SRC_DIR}/arg.cpp"
    "${SRC_DIR}/argparser.cpp"
)

add_library(${PROJECT_NAME}
    ${SRC_FILES}
    ${INCLUDE_FILES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${Boost_INCLUDE_DIR}
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)


target_link_libraries(${PROJECT_NAME}
    argparser
    gtest
)
