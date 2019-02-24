project(compressor_app)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDE_FILES
    "${INCLUDE_DIR}/cli.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(SRC_FILES
    "${SRC_DIR}/cli.cpp"
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

target_link_libraries(${PROJECT_NAME}
    compressor
    argparser
)
