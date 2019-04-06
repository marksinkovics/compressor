project(compressor_cli_test)

enable_testing()

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/cli.cpp"
)

add_library(${PROJECT_NAME} ${INCLUDE_FILES} ${SRC_FILES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${Boost_INCLUDE_DIR}
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)

target_link_libraries(${PROJECT_NAME}
    compressor_test
    argparser_test
    compressor_cli
    GTest::GTest
    GMock::GMock
)
