project(argparser_test)

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

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})

include(PackageTest)
package_add_test(
    NAME
        ${PROJECT_NAME}
    HEADERS
        ${INCLUDE_DIR}
        ${Boost_INCLUDE_DIR}
    SOURCES
        ${INCLUDE_FILES}
        ${SRC_FILES}
    LIBRARIES
        argparser
)
