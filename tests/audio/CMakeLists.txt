# Static tests for the ALSA UCM profile. None of them needs a sound card
# or touches audio: they only read the files below audio/ucm2.

set(FF802_UCM_DIR ${PROJECT_SOURCE_DIR}/audio/ucm2)

# Structure and content checks (CMake only, always available).
add_test(NAME ucm_profile_static
    COMMAND ${CMAKE_COMMAND} -DUCM_DIR=${FF802_UCM_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_ucm_profile.cmake)

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
else()
    # Reported by CTest as "Not Run (skipped)", never as passed.
    add_test(NAME ucm_alsa_syntax
        COMMAND ${CMAKE_COMMAND} -E echo
            "alsa-lib development files not found (rebuild the dev container)")
    set_tests_properties(ucm_alsa_syntax PROPERTIES
        SKIP_REGULAR_EXPRESSION "alsa-lib development files not found")
endif()

# Simulated test of the host test script T1 (fake UCM tree, fake procfs,
# fake alsaucm). Never touches the real ALSA configuration.
add_test(NAME ucm_t1_script
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_ucm_t1.sh
            ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/t1-scratch)
