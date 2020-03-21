project(example)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/main.cpp"
)

add_executable(${PROJECT_NAME}
    ${INCLUDE_FILES}
    ${SRC_FILES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${INCLUDE_DIR}
        ${Boost_INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)

set(SAMPLES ${PROJECT_SOURCE_DIR}/samples)
if("${CMAKE_GENERATOR}" STREQUAL "Xcode")
    file(COPY ${SAMPLES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Debug)
    file(COPY ${SAMPLES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Release)
else()
    file(COPY ${SAMPLES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()

target_link_libraries(${PROJECT_NAME}
    compressor_cli
)
