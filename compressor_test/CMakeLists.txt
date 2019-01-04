project(compressor_test)

enable_testing()

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/compressor/test.cpp"
    "${SRC_DIR}/compressor/compressor.cpp"
    "${SRC_DIR}/bitset/bitset.cpp"
    "${SRC_DIR}/bitset/bitset_iterator.cpp"
    "${SRC_DIR}/tree/binary/node.cpp"
    "${SRC_DIR}/tree/binary/tree.cpp"
    "${SRC_DIR}/tree/binary/nodeiterator.cpp"
    "${SRC_DIR}/utils/utils.cpp"
    "${SRC_DIR}/data/data.cpp"
)

add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${INCLUDE_FILES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC ${Boost_INCLUDE_DIR}
)
target_link_libraries(${PROJECT_NAME}
    compressor
    GTest::GTest
    GMock::GMock
    GMock::Main
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
# add_test(test_all ${PROJECT_NAME})
# add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
