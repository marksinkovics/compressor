project(tests)

enable_testing()

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/empty.cpp"
)

add_executable(${PROJECT_NAME}
    ${INCLUDE_FILES}
    ${SRC_FILES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC ${Boost_INCLUDE_DIR}
)

# Force load argparser_test lib
SET(ARGPARSER_TEST_LIB
    -Wl,-force_load argparser_test
)

# Force load compressor_cli_test lib
SET(COMPRESSOR_CLI_TEST_LIB
    -Wl,-force_load compressor_cli_test
)

# Force load compressor_cli_test lib
SET(COMPRESSOR_TEST_LIB
    -Wl,-force_load compressor_test
)

target_link_libraries(${PROJECT_NAME}
    ${ARGPARSER_TEST_LIB}
    ${COMPRESSOR_CLI_TEST_LIB}
    ${COMPRESSOR_TEST_LIB}
    GMock::Main
)

# include(GoogleTest)
# gtest_discover_tests(${PROJECT_NAME})
# add_test(test_all ${PROJECT_NAME})
# add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
