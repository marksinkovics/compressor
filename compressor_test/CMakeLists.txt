project(compressor_test)

enable_testing()

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/compressor/test.cpp"
    "${SRC_DIR}/bitvector/BitVectorTests.cpp"
)

add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${INCLUDE_FILES}
)

target_link_libraries(${PROJECT_NAME}
    compressor
    GTest::GTest
    GTest::Main
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
# add_test(test_all ${PROJECT_NAME})
# add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})


