cmake_minimum_required(VERSION 3.20)

project(fireface802-cc
    VERSION 0.4.1
    DESCRIPTION "Switch the original RME Fireface 802 between proprietary USB and class-compliant mode"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(FF802_WERROR "Treat compiler warnings as errors" ON)

# Version identification of ff802-cc and ff802-detect (--version):
# project version, Git revision (12 hex digits), build type, write gate.
# The revision is determined here, at configure time: "+dirty" if tracked
# files differ from HEAD (untracked files are not part of the build),
# "unknown" without Git. A new commit re-runs the configuration (HEAD and
# the branch ref are configure dependencies). The release packages are
# built from a clean checkout in a fresh build directory, so the same
# commit always gives the same programs.
set(FF802_GIT_REVISION "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE git_rev RESULT_VARIABLE git_rc
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(git_rc EQUAL 0 AND git_rev MATCHES "^[0-9a-f]+$")
        set(FF802_GIT_REVISION "${git_rev}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                    status --porcelain --untracked-files=no
            OUTPUT_VARIABLE git_changes RESULT_VARIABLE git_rc2 ERROR_QUIET)
        if(NOT git_rc2 EQUAL 0 OR NOT git_changes STREQUAL "")
            string(APPEND FF802_GIT_REVISION "+dirty")
        endif()
    endif()
    set(git_dir "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/HEAD")
    file(READ "${git_dir}/HEAD" git_head)
    if(git_head MATCHES "^ref: ([^\n]+)")
        if(EXISTS "${git_dir}/${CMAKE_MATCH_1}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                         "${git_dir}/${CMAKE_MATCH_1}")
        endif()
    endif()
    if(EXISTS "${git_dir}/packed-refs")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/packed-refs")
    endif()
endif()
# Read back by packaging/build-deb-cc.sh to verify the compiled revision.
set(FF802_GIT_REVISION_DETECTED "${FF802_GIT_REVISION}" CACHE INTERNAL
    "Git revision compiled into ff802-cc and ff802-detect")
if(CMAKE_BUILD_TYPE)
    set(FF802_BUILD_TYPE_NAME "${CMAKE_BUILD_TYPE}")
else()
    set(FF802_BUILD_TYPE_NAME "unspecified")
endif()
message(STATUS "fireface802-cc ${PROJECT_VERSION}, git ${FF802_GIT_REVISION}, "
               "${FF802_BUILD_TYPE_NAME}")

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
    src/cc_mode.c
    src/mixer_init.c)
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

# Pure CC switch sequence on an abstract USB backend (no libusb).
add_library(ff802_switch STATIC
    src/usb_backend.c
    src/cc_switch.c
    src/cc_disable.c)
target_include_directories(ff802_switch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ff802_switch PUBLIC ff802_protocol ff802_detect)
ff802_set_warnings(ff802_switch)

# T4 automation: sysfs identity, per-connection state, events, and the
# switch flow on an abstract USB backend. No libusb here.
add_library(ff802_auto STATIC
    src/auto_sysfs.c
    src/auto_state.c
    src/auto_switch.c
    src/usb_port.c
    src/mixer_port.c
    src/mixer_run.c
    src/disable_check.c)
target_include_directories(ff802_auto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ff802_auto PUBLIC ff802_switch)
ff802_set_warnings(ff802_auto)

# Loopback measurement core of ff802-chanmap (hardware test T2): signal
# generation, guards, detection, flow, reservation, report. No ALSA here,
# so the complete flow is testable against a simulated device.
add_library(ff802_chanmap STATIC
    src/chanmap_dsp.c
    src/chanmap_measure.c
    src/chanmap_card.c
    src/chanmap_reserve.c
    src/chanmap_report.c)
target_include_directories(ff802_chanmap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ff802_chanmap PUBLIC m)
# POSIX/GNU functions (fork, prctl, nanosleep); the correlation runs fast
# enough for the tests only when optimised, also in Debug builds.
set_target_properties(ff802_chanmap PROPERTIES C_EXTENSIONS ON)
target_compile_options(ff802_chanmap PRIVATE -O2)
ff802_set_warnings(ff802_chanmap)

# ALSA backend and the ff802-chanmap program need alsa-lib.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(ALSA QUIET IMPORTED_TARGET GLOBAL alsa)
endif()
set(FF802_HAVE_CHANMAP_TOOL OFF)
if(ALSA_FOUND)
    set(FF802_HAVE_CHANMAP_TOOL ON)
    add_library(ff802_chanmap_alsa STATIC src/chanmap_alsa.c)
    target_link_libraries(ff802_chanmap_alsa PUBLIC ff802_chanmap PkgConfig::ALSA)
    set_target_properties(ff802_chanmap_alsa PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(ff802_chanmap_alsa)

    add_executable(ff802-chanmap tools/ff802_chanmap.c)
    target_link_libraries(ff802-chanmap PRIVATE ff802_chanmap_alsa)
    target_compile_definitions(ff802-chanmap PRIVATE FF802_VERSION="${PROJECT_VERSION}")
    set_target_properties(ff802-chanmap PROPERTIES
        C_EXTENSIONS ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    ff802_set_warnings(ff802-chanmap)
else()
    message(WARNING "alsa-lib not found: ff802-chanmap is not built")
endif()

# Safety gate of the mixer initialisation: without this option the ALSA
# sequencer backend can list ports but cannot subscribe to the Fireface
# MIDI port or send a SysEx message. Keep OFF until a hardware test of
# the mixer writes has been explicitly approved.
option(FF802_ENABLE_MIXER_WRITE "Compile in MIDI subscribe and SysEx send of the mixer initialisation" OFF)
if(FF802_ENABLE_MIXER_WRITE)
    message(WARNING "FF802_ENABLE_MIXER_WRITE=ON: ff802-cc-autoswitch can write mixer registers")
endif()

# ALSA sequencer backend of the mixer initialisation (ff802-cc-autoswitch).
if(ALSA_FOUND)
    add_library(ff802_midi_alsa STATIC src/midi_seq_alsa.c)
    target_include_directories(ff802_midi_alsa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(ff802_midi_alsa PUBLIC PkgConfig::ALSA)
    if(FF802_ENABLE_MIXER_WRITE)
        target_compile_definitions(ff802_midi_alsa PRIVATE FF802_ENABLE_MIXER_WRITE=1)
    else()
        target_compile_definitions(ff802_midi_alsa PRIVATE FF802_ENABLE_MIXER_WRITE=0)
    endif()
    set_target_properties(ff802_midi_alsa PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(ff802_midi_alsa)
endif()

# Hardware test B.2 (tools/hw-test/ff802_mixer_b2.c): ONE write of register
# 0x2600 through the ALSA sequencer. The only program that compiles the
# ALSA backend with its write functions (FF802_ENABLE_MIXER_WRITE=1 for
# this target only; the global option above stays OFF). Default OFF, never
# installed, not part of any package.
option(FF802_BUILD_MIXER_B2 "Build the hardware test program ff802-mixer-b2 (one MIDI register write)" OFF)
set(FF802_HAVE_MIXER_B2 OFF)
if(FF802_BUILD_MIXER_B2)
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR "FF802_BUILD_MIXER_B2 needs alsa-lib (libasound2-dev)")
    endif()
    message(WARNING "FF802_BUILD_MIXER_B2=ON: ff802-mixer-b2 can send one mixer register to the device")
    add_executable(ff802-mixer-b2 tools/hw-test/ff802_mixer_b2.c src/midi_seq_alsa.c)
    target_link_libraries(ff802-mixer-b2 PRIVATE ff802_auto PkgConfig::ALSA)
    target_compile_definitions(ff802-mixer-b2 PRIVATE
        FF802_ENABLE_MIXER_WRITE=1 FF802_VERSION="${PROJECT_VERSION}")
    set_target_properties(ff802-mixer-b2 PROPERTIES
        C_EXTENSIONS ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    ff802_set_warnings(ff802-mixer-b2)
    set(FF802_HAVE_MIXER_B2 ON)
endif()

# Hardware test B.3 (tools/hw-test/ff802_mixer_b3.c): ONE write of the
# routing register 0x4228 = 0x9000 (Playback 9 -> Output 9, 0 dB). Same
# rules as B.2: default OFF, writes only in this target, never installed.
option(FF802_BUILD_MIXER_B3 "Build the hardware test program ff802-mixer-b3 (one MIDI routing write)" OFF)
set(FF802_HAVE_MIXER_B3 OFF)
if(FF802_BUILD_MIXER_B3)
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR "FF802_BUILD_MIXER_B3 needs alsa-lib (libasound2-dev)")
    endif()
    message(WARNING "FF802_BUILD_MIXER_B3=ON: ff802-mixer-b3 can send one mixer register to the device")
    add_executable(ff802-mixer-b3 tools/hw-test/ff802_mixer_b3.c src/midi_seq_alsa.c)
    target_link_libraries(ff802-mixer-b3 PRIVATE ff802_auto PkgConfig::ALSA)
    target_compile_definitions(ff802-mixer-b3 PRIVATE
        FF802_ENABLE_MIXER_WRITE=1 FF802_VERSION="${PROJECT_VERSION}")
    set_target_properties(ff802-mixer-b3 PROPERTIES
        C_EXTENSIONS ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    ff802_set_warnings(ff802-mixer-b3)
    set(FF802_HAVE_MIXER_B3 ON)
endif()

# Command-line tools need libusb. Without it, only the pure libraries and
# their tests are built.
option(FF802_BUILD_TOOLS "Build the libusb based command-line tools" ON)

# Safety gate: without this option the binaries cannot claim a USB
# interface or perform a USB write. Keep OFF until a real write test has
# been explicitly approved.
option(FF802_ENABLE_USB_WRITE "Compile in USB claim and bulk write support" OFF)
if(FF802_ENABLE_USB_WRITE)
    message(WARNING "FF802_ENABLE_USB_WRITE=ON: ff802-cc can send commands to the device")
endif()

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
