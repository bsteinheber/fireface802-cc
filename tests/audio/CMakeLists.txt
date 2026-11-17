# Static tests for the ALSA UCM profile. None of them needs a sound card
# or touches audio: they only read the files below audio/ucm2.

set(FF802_UCM_DIR ${PROJECT_SOURCE_DIR}/audio/ucm2)

# Structure and content checks (CMake only, always available).
add_test(NAME ucm_profile_static
    COMMAND ${CMAKE_COMMAND} -DUCM_DIR=${FF802_UCM_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_ucm_profile.cmake)

# Tests that need alsa-lib or the system UCM tree are skipped when these
# are missing (CTest: "Not Run (Skipped)"; CTest still prints "100% tests
# passed"). With FF802_REQUIRE_ALSA_TESTS such a test FAILS instead, so a
# missing package cannot go unnoticed. The dev container sets the
# environment variable FF802_REQUIRE_ALSA_TESTS=ON (devcontainer.json).
# When the variable is set it always wins over the cache, so a build
# directory configured before the container rebuild cannot keep OFF.
option(FF802_REQUIRE_ALSA_TESTS
    "Fail instead of skip ALSA/UCM tests whose prerequisites are missing" OFF)
if(DEFINED ENV{FF802_REQUIRE_ALSA_TESTS})
    set(FF802_REQUIRE_ALSA_TESTS "$ENV{FF802_REQUIRE_ALSA_TESTS}" CACHE BOOL
        "Fail instead of skip ALSA/UCM tests whose prerequisites are missing" FORCE)
endif()
message(STATUS "FF802_REQUIRE_ALSA_TESTS: ${FF802_REQUIRE_ALSA_TESTS}")

function(ff802_alsa_test_unavailable name reason)
    if(FF802_REQUIRE_ALSA_TESTS)
        add_test(NAME ${name} COMMAND ${CMAKE_COMMAND}
            -E env "FF802_REASON=${name} required but not available: ${reason}"
            ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/fail_with_reason.cmake)
    else()
        # Reported by CTest as "Not Run (Skipped)", never as passed.
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND} -E echo "${name} skipped: ${reason}")
        set_tests_properties(${name} PROPERTIES
            SKIP_REGULAR_EXPRESSION "${name} skipped")
    endif()
endfunction()

# Syntax check with the real alsa-lib configuration parser.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(ALSA QUIET IMPORTED_TARGET alsa)
endif()

if(ALSA_FOUND)
    add_executable(test_ucm_syntax test_ucm_syntax.c)
    target_link_libraries(test_ucm_syntax PRIVATE PkgConfig::ALSA)
    # alsa/asoundlib.h needs the GNU/POSIX feature set.
    set_target_properties(test_ucm_syntax PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(test_ucm_syntax)
    add_test(NAME ucm_alsa_syntax COMMAND test_ucm_syntax ${FF802_UCM_DIR})

    # Read-only alsaucm stand-in and fake Fireface 802 control device for
    # the evaluation test below.
    add_executable(ucm_query ucm_query.c)
    target_link_libraries(ucm_query PRIVATE PkgConfig::ALSA)
    set_target_properties(ucm_query PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(ucm_query)

    add_library(fake_ctl_ff802 MODULE fake_ctl_ff802.c)
    target_link_libraries(fake_ctl_ff802 PRIVATE PkgConfig::ALSA)
    # PIC selects the versioned plugin entry symbol (alsa/global.h).
    target_compile_definitions(fake_ctl_ff802 PRIVATE PIC)
    set_target_properties(fake_ctl_ff802 PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(fake_ctl_ff802)

    # Fake hardware PCM (22 channels, S24_3LE, measures playback peaks) for
    # the PipeWire integration test.
    add_library(fake_pcm_ff802 MODULE fake_pcm_ff802.c)
    target_link_libraries(fake_pcm_ff802 PRIVATE PkgConfig::ALSA)
    target_compile_definitions(fake_pcm_ff802 PRIVATE PIC)
    set_target_properties(fake_pcm_ff802 PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(fake_pcm_ff802)
endif()

# Evaluation of the profile by the real alsa-lib with a fake card: runs the
# T1 script in the container. Needs the system UCM tree (package
# alsa-ucm-conf) and alsa-lib 1.2.15.x, whose private control layout the
# fake card relies on.
set(FF802_SYSTEM_UCM2 /usr/share/alsa/ucm2 CACHE PATH
    "System UCM tree used by the ucm_alsa_eval test (read only)")
set(ucm_eval_skip "")
if(NOT ALSA_FOUND)
    set(ucm_eval_skip "alsa-lib development files not found (rebuild the dev container)")
elseif(NOT ALSA_VERSION MATCHES "^1\\.2\\.15(\\.|$)")
    set(ucm_eval_skip "fake control device supports alsa-lib 1.2.15.x only, found ${ALSA_VERSION}")
elseif(NOT EXISTS ${FF802_SYSTEM_UCM2}/USB-Audio/USB-Audio.conf)
    set(ucm_eval_skip "system UCM tree not found: ${FF802_SYSTEM_UCM2} (alsa-ucm-conf missing, rebuild the dev container)")
endif()
# also used by tests/packaging
set(FF802_UCM_EVAL_SKIP "${ucm_eval_skip}" PARENT_SCOPE)
if(ucm_eval_skip STREQUAL "")
    add_test(NAME ucm_alsa_eval
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_ucm_eval.sh
                ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
                ${FF802_SYSTEM_UCM2} ${ALSA_VERSION}
                ${CMAKE_CURRENT_BINARY_DIR}/eval-scratch)
    set_tests_properties(ucm_alsa_eval PROPERTIES
        REQUIRED_FILES "$<TARGET_FILE:ucm_query>;$<TARGET_FILE:fake_ctl_ff802>")
else()
    ff802_alsa_test_unavailable(ucm_alsa_eval "${ucm_eval_skip}")
endif()

if(NOT ALSA_FOUND)
    ff802_alsa_test_unavailable(ucm_alsa_syntax
        "alsa-lib development files not found (rebuild the dev container)")
endif()

# Simulated test of the host test script T1 (fake UCM tree, fake procfs,
# fake alsaucm). Never touches the real ALSA configuration.
add_test(NAME ucm_t1_script
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_ucm_t1.sh
            ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/t1-scratch)

# PipeWire/WirePlumber integration (T3) with a fake Fireface 802: a private
# PipeWire session (own runtime, config and state directories below the
# scratch dir) runs the unchanged WirePlumber ALSA monitor, ACP, UCM
# SplitPCM and the loopbacks against the fake control and PCM plugins.
# The fake enumerator replaces only PipeWire's udev enumerator. Needs the
# packages pipewire, pipewire-bin, pipewire-pulse, wireplumber,
# pulseaudio-utils and libspa-0.2-dev (dev container). FF802_PW_ROOT can
# point to an unpacked tree of these packages instead of /.
set(FF802_PW_ROOT / CACHE PATH
    "Root of the PipeWire/WirePlumber installation used by pw_integration")
set(pw_skip "${ucm_eval_skip}")
set(pw_spa_include "")
if(pw_skip STREQUAL "")
    if(FF802_PW_ROOT STREQUAL "/")
        pkg_check_modules(SPA QUIET libspa-0.2)
        if(SPA_FOUND)
            set(pw_spa_include ${SPA_INCLUDE_DIRS})
        endif()
    elseif(EXISTS ${FF802_PW_ROOT}/usr/include/spa-0.2/spa/monitor/device.h)
        set(pw_spa_include ${FF802_PW_ROOT}/usr/include/spa-0.2)
    endif()
    if(pw_spa_include STREQUAL "")
        set(pw_skip "libspa-0.2 headers not found (libspa-0.2-dev missing, rebuild the dev container)")
    endif()
endif()
if(pw_skip STREQUAL "")
    foreach(tool pipewire wireplumber pipewire-pulse pw-cli pw-cat pw-record wpctl pactl pacat)
        if(NOT EXISTS ${FF802_PW_ROOT}/usr/bin/${tool})
            set(pw_skip "${tool} not found below ${FF802_PW_ROOT} (pipewire/wireplumber/pulseaudio-utils missing, rebuild the dev container)")
            break()
        endif()
    endforeach()
endif()
if(pw_skip STREQUAL "")
    add_library(fake_spa_enum_ff802 MODULE fake_spa_enum_ff802.c)
    target_include_directories(fake_spa_enum_ff802 SYSTEM PRIVATE ${pw_spa_include})
    set_target_properties(fake_spa_enum_ff802 PROPERTIES
        C_EXTENSIONS ON
        OUTPUT_NAME spa-ff802-fake-enum
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/spa/ff802-test)
    ff802_set_warnings(fake_spa_enum_ff802)
    add_test(NAME pw_integration
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_pw_integration.sh
                ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
                ${FF802_PW_ROOT} ${FF802_SYSTEM_UCM2}
                ${CMAKE_CURRENT_BINARY_DIR}/pw-scratch)
    set_tests_properties(pw_integration PROPERTIES
        TIMEOUT 300
        REQUIRED_FILES "$<TARGET_FILE:fake_ctl_ff802>;$<TARGET_FILE:fake_pcm_ff802>;$<TARGET_FILE:fake_spa_enum_ff802>")
else()
    ff802_alsa_test_unavailable(pw_integration "${pw_skip}")
endif()
