macro(package_add_test)
    set(options)
    set(oneValueArgs NAME)
    set(multiValueArgs HEADERS SOURCES LIBRARIES)
    cmake_parse_arguments(ADD_PACKAGE_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

    # message(STATUS "ADD_PACKAGE_TEST_NAME: ${ADD_PACKAGE_TEST_NAME}")
    # message(STATUS "ADD_PACKAGE_TEST_HEADERS: ${ADD_PACKAGE_TEST_HEADERS}")
    # message(STATUS "ADD_PACKAGE_TEST_SOURCES: ${ADD_PACKAGE_TEST_SOURCES}")
    # message(STATUS "ADD_PACKAGE_TEST_LIBRARIES: ${ADD_PACKAGE_TEST_LIBRARIES}")

    enable_testing()

    add_library(${ADD_PACKAGE_TEST_NAME}
        ${ADD_PACKAGE_TEST_SOURCES}
    )

    target_include_directories(${ADD_PACKAGE_TEST_NAME}
        PUBLIC
            ${ADD_PACKAGE_TEST_HEADERS}
        PRIVATE
            ${ADD_PACKAGE_TEST_SOURCES}
    )

    target_link_libraries(${ADD_PACKAGE_TEST_NAME}
        ${ADD_PACKAGE_TEST_LIBRARIES}
        gtest
        gmock
    )

    add_executable("${ADD_PACKAGE_TEST_NAME}_app"
        ${ADD_PACKAGE_TEST_SOURCES}
    )

    target_include_directories("${ADD_PACKAGE_TEST_NAME}_app"
        PUBLIC
            ${ADD_PACKAGE_TEST_HEADERS}
        PRIVATE
            ${ADD_PACKAGE_TEST_SOURCES}
    )

    target_link_libraries("${ADD_PACKAGE_TEST_NAME}_app"
        ${ADD_PACKAGE_TEST_LIBRARIES}
        gtest
        gmock
        gtest_main
    )
    include(GoogleTest)
    # gtest_add_tests(
    #     TARGET "${ADD_PACKAGE_TEST_NAME}_app"
    #     TEST_LIST AUTO
    # )

    gtest_discover_tests("${ADD_PACKAGE_TEST_NAME}_app"
        WORKING_DIRECTORY ${PROJECT_DIR}
        PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_DIR}"
        TEST_LIST AUTO
    )

    set_target_properties(${ADD_PACKAGE_TEST_NAME} PROPERTIES FOLDER tests)
    set_target_properties("${ADD_PACKAGE_TEST_NAME}_app" PROPERTIES FOLDER tests)

endmacro()