# Debian package ff802-ucm-profile: build target and isolated package test.

find_program(FF802_DPKG_DEB dpkg-deb)
find_program(FF802_DPKG dpkg)

if(FF802_DPKG_DEB)
    # cmake --build <dir> --target deb  ->  <dir>/deb/ff802-ucm-profile_<v>_all.deb
    add_custom_target(deb
        COMMAND sh ${PROJECT_SOURCE_DIR}/packaging/build-deb.sh ${CMAKE_BINARY_DIR}/deb
        COMMENT "Building the ff802-ucm-profile Debian package"
        VERBATIM)
endif()

# The test evaluates the installed tree with the real alsa-lib (needs the
# same prerequisites as ucm_alsa_eval) and installs into an isolated root
# with stand-in packages built from the system UCM tree.
set(deb_skip "")
if(NOT FF802_DPKG_DEB OR NOT FF802_DPKG)
    set(deb_skip "dpkg/dpkg-deb not found")
elseif(NOT EXISTS ${FF802_SYSTEM_UCM2}/USB-Audio/USB-Audio.conf)
    set(deb_skip "system UCM tree not found: ${FF802_SYSTEM_UCM2} (alsa-ucm-conf missing, rebuild the dev container)")
elseif(NOT FF802_UCM_EVAL_SKIP STREQUAL "")
    set(deb_skip "${FF802_UCM_EVAL_SKIP}")
endif()

if(deb_skip STREQUAL "")
    add_test(NAME deb_package
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_deb.sh
                ${PROJECT_SOURCE_DIR} ${FF802_SYSTEM_UCM2}
                ${CMAKE_BINARY_DIR}/tests/audio
                ${CMAKE_CURRENT_BINARY_DIR}/deb-scratch)
    set_tests_properties(deb_package PROPERTIES
        REQUIRED_FILES "$<TARGET_FILE:ucm_query>;$<TARGET_FILE:fake_ctl_ff802>")
else()
    ff802_alsa_test_unavailable(deb_package "${deb_skip}")
endif()

# Debian package ff802-cc-auto (T4): build target and isolated package test.
# cmake --build <dir> --target deb-auto  ->  <dir>/deb/ff802-cc-auto_<v>_<arch>.deb
if(FF802_DPKG_DEB AND FF802_HAVE_TOOLS)
    add_custom_target(deb-auto
        COMMAND sh ${PROJECT_SOURCE_DIR}/packaging/build-deb-auto.sh ${CMAKE_BINARY_DIR}/deb
        COMMENT "Building the ff802-cc-auto Debian package"
        VERBATIM)
endif()
if(FF802_DPKG_DEB AND FF802_DPKG AND FF802_HAVE_TOOLS)
    add_test(NAME deb_auto_package
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_deb_auto.sh
                ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/deb-auto-scratch)
    set_tests_properties(deb_auto_package PROPERTIES TIMEOUT 900)
endif()
