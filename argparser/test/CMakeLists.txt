project(argparser_test)

enable_testing()

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

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
    PUBLIC ${Boost_INCLUDE_DIR}
    PUBLIC ${gtest_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
    argparser
)
