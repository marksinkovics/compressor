project(ingot)

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
    PRIVATE
        ${SRC_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        compressor_cli
)

set(SAMPLES_DIR "${CMAKE_SOURCE_DIR}/compressor_example/samples/raw")
foreach(sample empty lorem_50p.txt flower.png)
    if(sample STREQUAL "empty")
        set(input "")
    else()
        set(input "${SAMPLES_DIR}/${sample}")
    endif()
    add_test(NAME PipeRoundTrip.${sample}
        COMMAND "${CMAKE_COMMAND}"
            "-DAPP=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DINPUT=${input}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/pipe_roundtrip/${sample}"
            -P "${PROJECT_SOURCE_DIR}/test/pipe_roundtrip.cmake"
    )
endforeach()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME CompareHarness.Samples
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/compare.py"
            --corpus --dir "${SAMPLES_DIR}" --tools ingot --no-save
            --app "$<TARGET_FILE:${PROJECT_NAME}>"
    )
endif()

find_program(MAGICK magick)
if(Python3_Interpreter_FOUND AND MAGICK)
    add_test(NAME ImagesHarness.Lossless
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/images.py"
            --corpus "${PROJECT_SOURCE_DIR}/test/pictures" --lossless --coders ingot --jobs 2
            --no-save --app "$<TARGET_FILE:${PROJECT_NAME}>"
    )
endif()

# The WebAssembly demo, when emsdk is in build/emsdk (docs/development.md, "WebAssembly demo").
if(EXISTS "${CMAKE_SOURCE_DIR}/build/emsdk/emsdk_env.sh")
    add_test(NAME Web.Demo COMMAND bash "${CMAKE_SOURCE_DIR}/tools/web.sh" test)
endif()
