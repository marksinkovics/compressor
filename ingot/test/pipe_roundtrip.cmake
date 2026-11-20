# cmake -DAPP=<ingot> -DINPUT=<file, or empty for an empty input> -DWORK_DIR=<dir> -P
foreach(var APP WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "Set ${var}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
if(INPUT STREQUAL "")
    set(INPUT "${WORK_DIR}/empty")
    file(WRITE "${INPUT}" "")
endif()

function(expect_success results what)
    foreach(result IN LISTS results)
        if(NOT result STREQUAL "0")
            message(FATAL_ERROR "${what} failed: ${results}")
        endif()
    endforeach()
endfunction()

function(expect_same_files a b)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${a}" "${b}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${a} and ${b} differ")
    endif()
endfunction()

execute_process(COMMAND "${APP}" encode -i "${INPUT}" -o "${WORK_DIR}/file.ingot"
                RESULTS_VARIABLE results)
expect_success("${results}" "encode between files")

execute_process(COMMAND "${APP}" encode
                INPUT_FILE "${INPUT}" OUTPUT_FILE "${WORK_DIR}/piped.ingot"
                RESULTS_VARIABLE results)
expect_success("${results}" "encode from stdin to stdout")
expect_same_files("${WORK_DIR}/file.ingot" "${WORK_DIR}/piped.ingot")

execute_process(COMMAND "${APP}" encode -i - -o -
                COMMAND "${APP}" decode
                INPUT_FILE "${INPUT}" OUTPUT_FILE "${WORK_DIR}/decoded"
                RESULTS_VARIABLE results)
expect_success("${results}" "encode | decode")
expect_same_files("${INPUT}" "${WORK_DIR}/decoded")
