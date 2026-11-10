# Every port vcpkg installed, transitive and host ones included, must be pinned to its exact
# version in vcpkg.json's "overrides", so a baseline bump or a new dependency can't change a
# version silently.
function(compressor_check_vcpkg_pins)
    file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" manifest)
    string(JSON count LENGTH "${manifest}" overrides)
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        string(JSON name GET "${manifest}" overrides ${i} name)
        string(JSON version GET "${manifest}" overrides ${i} version)
        set(pin_${name} "${version}")
    endforeach()

    # One <port>_<version>_<triplet>.list per installed port; the version has no port-version.
    file(GLOB lists RELATIVE "${VCPKG_INSTALLED_DIR}/vcpkg/info"
         "${VCPKG_INSTALLED_DIR}/vcpkg/info/*.list")
    set(errors "")
    foreach(list IN LISTS lists)
        if(NOT list MATCHES "^([a-z0-9-]+)_(.+)_([a-z0-9-]+)\\.list$")
            continue()
        endif()
        set(port "${CMAKE_MATCH_1}")
        set(version "${CMAKE_MATCH_2}")
        if(NOT DEFINED pin_${port})
            string(APPEND errors "\n  ${port} ${version} has no override")
        elseif(NOT pin_${port} STREQUAL version)
            string(APPEND errors "\n  ${port} is ${version}, but the override says ${pin_${port}}")
        endif()
    endforeach()
    if(errors)
        message(FATAL_ERROR "vcpkg ports not pinned in vcpkg.json \"overrides\":${errors}")
    endif()
endfunction()
