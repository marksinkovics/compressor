project(compressor_test)

enable_testing()

add_executable(${PROJECT_NAME}
	src/compressor/test.cpp
	src/bitvector/BitVectorTests.cpp
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


