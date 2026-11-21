project(fuzz)

# Without COMPRESSOR_FUZZ each target replays its checked-in corpus once under ctest, so every
# input that once crashed stays a regression test. With it, ctest fuzzes each target briefly.
set(FUZZ_TARGETS container archive stats inflate image)
set(CORPUS_DIR "${PROJECT_SOURCE_DIR}/corpus")

foreach(name IN LISTS FUZZ_TARGETS)
    add_executable(fuzz_${name} src/${name}.cpp src/dictionary.h src/discard.h)
    target_link_libraries(fuzz_${name} PRIVATE compressor ZLIB::ZLIB)
    set_target_properties(fuzz_${name} PROPERTIES FOLDER fuzz)
    if(COMPRESSOR_FUZZ)
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        set(found "${CMAKE_CURRENT_BINARY_DIR}/corpus/${name}")
        file(MAKE_DIRECTORY "${found}")
        add_test(NAME Fuzz.${name}
            COMMAND fuzz_${name} -runs=50000 -max_len=65536 "${found}" "${CORPUS_DIR}/${name}")
    else()
        target_sources(fuzz_${name} PRIVATE src/replay_main.cpp)
        add_test(NAME FuzzCorpus.${name} COMMAND fuzz_${name} "${CORPUS_DIR}/${name}")
    endif()
endforeach()

add_executable(fuzz_make_seeds src/make_seeds.cpp src/dictionary.h)
target_link_libraries(fuzz_make_seeds PRIVATE compressor)
set_target_properties(fuzz_make_seeds PROPERTIES FOLDER fuzz)
