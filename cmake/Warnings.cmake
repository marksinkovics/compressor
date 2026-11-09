# Linked PRIVATE into our own targets only, so googletest and benchmark keep their own flags.
add_library(compressor_warnings INTERFACE)

if(MSVC)
    target_compile_options(compressor_warnings INTERFACE /W4)
    if(COMPRESSOR_WERROR)
        target_compile_options(compressor_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(compressor_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(COMPRESSOR_WERROR)
        target_compile_options(compressor_warnings INTERFACE -Werror)
    endif()
endif()
