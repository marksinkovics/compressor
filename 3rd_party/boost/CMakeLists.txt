#
# Helps
#
# https://www.jetbrains.com/help/clion/quick-cmake-tutorial.html#boost
# https://rix0r.nl/blog/2015/08/13/cmake-guide/
# http://cpprocks.com/using-cmake-to-build-a-cross-platform-project-with-a-boost-dependency/


# Include Boost as an imported target

set (Boost_USE_STATIC_LIBS ON) # enable dynamic linking
set (Boost_USE_MULTITHREAD ON)  # enable multithreading
set (Boost_USE_STATIC_RUNTIME OFF)
list(APPEND CMAKE_MODULE_PATH ./cmake)
set(BOOST_REQUESTED_VERSION 1.68)

find_package (Boost 1.68.0 REQUIRED)

IF (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIR})
endif()

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})