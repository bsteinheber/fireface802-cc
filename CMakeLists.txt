cmake_minimum_required(VERSION 3.20)

project(fireface802-cc
    VERSION 0.2.0
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

# Pure protocol library: no USB, no I/O.
add_library(ff802_protocol STATIC
    src/reg_codec.c
    src/cc_mode.c)
target_include_directories(ff802_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
ff802_set_warnings(ff802_protocol)

# Pure device detection logic: classification, matching, reporting.
# Works on plain device records and does not depend on libusb.
add_library(ff802_detect STATIC
    src/device_id.c
    src/detect.c)
target_include_directories(ff802_detect PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
ff802_set_warnings(ff802_detect)

# Command-line tools need libusb. Without it, only the pure libraries and
# their tests are built.
option(FF802_BUILD_TOOLS "Build the libusb based command-line tools" ON)
set(FF802_HAVE_TOOLS OFF)
if(FF802_BUILD_TOOLS)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0>=1.0.22)
    endif()
    if(LIBUSB_FOUND)
        set(FF802_HAVE_TOOLS ON)
        add_subdirectory(tools)
    else()
        message(WARNING "libusb-1.0 not found: command-line tools are not built")
    endif()
endif()

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
