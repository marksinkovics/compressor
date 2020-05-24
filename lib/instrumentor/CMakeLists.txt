project(instrumentor)

set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

SET(INSTRUMENTOR_INCLUDE_DIR "${INCLUDE_DIR}/instrumentor")

set(INCLUDE_FILES
    "${INSTRUMENTOR_INCLUDE_DIR}/instrumentor.h"
    "${INSTRUMENTOR_INCLUDE_DIR}/instrumentationtimer.h"
    "${INSTRUMENTOR_INCLUDE_DIR}/profileresult.h"
)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
SET(INSTRUMENTOR_SRC_DIR "${SRC_DIR}/instrumentor")
set(SRC_FILES
    "${INSTRUMENTOR_SRC_DIR}/instrumentor.cpp"
    "${INSTRUMENTOR_SRC_DIR}/instrumentationtimer.cpp"
)

add_library(${PROJECT_NAME} ${INCLUDE_FILES} ${SRC_FILES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${INCLUDE_DIR}
    PRIVATE
        ${SRC_DIR}
)

 add_subdirectory(example)
