cmake_minimum_required(VERSION 3.20)

project(fireface802-cc
    VERSION 0.1.0
    DESCRIPTION "Switch the original RME Fireface 802 between proprietary USB and class-compliant mode"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(FF802_WERROR "Treat compiler warnings as errors" ON)

# Common warning flags, applied per target via ff802_set_warnings().
function(ff802_set_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes)
        if(FF802_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
