project(compressor_cli_test)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/cli.cpp"
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

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
        compressor_test
        argparser_test
        compressor_cli
)