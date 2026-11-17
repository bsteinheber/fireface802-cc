# Tests of ff802-chanmap. None of them opens a sound card: the flow runs
# against a simulated device, the ALSA backend against the "null" plugin,
# and the command line only up to the point before card detection.

add_library(chanmap_sim STATIC sim_backend.c)
target_link_libraries(chanmap_sim PUBLIC ff802_chanmap)
set_target_properties(chanmap_sim PROPERTIES C_EXTENSIONS ON)
target_compile_options(chanmap_sim PRIVATE -O2)
ff802_set_warnings(chanmap_sim)

function(chanmap_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE chanmap_sim)
    set_target_properties(${name} PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(${name})
endfunction()

chanmap_test(test_chanmap_dsp)
add_test(NAME test_chanmap_dsp COMMAND test_chanmap_dsp)

chanmap_test(test_chanmap_sim)
add_test(NAME test_chanmap_sim COMMAND test_chanmap_sim)

chanmap_test(test_chanmap_card)
add_test(NAME test_chanmap_card
    COMMAND test_chanmap_card ${CMAKE_CURRENT_BINARY_DIR}/card-scratch)

chanmap_test(test_chanmap_reserve)
add_test(NAME test_chanmap_reserve
    COMMAND test_chanmap_reserve ${CMAKE_CURRENT_SOURCE_DIR}/fake-pw-reserve.sh
            ${CMAKE_CURRENT_BINARY_DIR}/reserve-scratch)

chanmap_test(test_chanmap_report)
add_test(NAME test_chanmap_report
    COMMAND test_chanmap_report ${CMAKE_CURRENT_BINARY_DIR}/report-scratch)
set_tests_properties(test_chanmap_report PROPERTIES FIXTURES_SETUP chanmap_results)

# Source check: device selection only through hw:CARD=<id>.
add_test(NAME check_chanmap_sources
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_chanmap_sources.cmake)

if(FF802_HAVE_CHANMAP_TOOL)
    add_executable(test_chanmap_alsa_null test_chanmap_alsa_null.c)
    target_link_libraries(test_chanmap_alsa_null PRIVATE ff802_chanmap_alsa)
    set_target_properties(test_chanmap_alsa_null PROPERTIES C_EXTENSIONS ON)
    ff802_set_warnings(test_chanmap_alsa_null)
    add_test(NAME test_chanmap_alsa_null COMMAND test_chanmap_alsa_null)

    function(chanmap_cli_test name rc regex)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCMD=$<TARGET_FILE:ff802-chanmap> "-DARGS=${ARGN}"
                -DEXPECT_RC=${rc} "-DEXPECT_REGEX=${regex}"
                -P ${PROJECT_SOURCE_DIR}/tests/run_expect.cmake)
    endfunction()
    # Only paths that end before the card is looked up: these tests must be
    # harmless even on a host with a Fireface 802 connected.
    chanmap_cli_test(chanmap_cli_help 0 "Usage: ff802-chanmap" "--help")
    chanmap_cli_test(chanmap_cli_version 0 "^ff802-chanmap ${PROJECT_VERSION}\n$" "--version")
    chanmap_cli_test(chanmap_cli_no_command 2 "Usage: ff802-chanmap")
    chanmap_cli_test(chanmap_cli_unknown_command 2 "unknown command 'play'" "play")
    chanmap_cli_test(chanmap_cli_no_position 2 "--position expects" "measure")
    chanmap_cli_test(chanmap_cli_bad_position 2 "--position expects" "measure|--position|out9")
    chanmap_cli_test(chanmap_cli_bad_input 2 "--input expects 1 or 2" "measure|--position|out1|--input|3")
    chanmap_cli_test(chanmap_cli_no_level_option 2 "unexpected argument '--level'"
        "measure|--position|out1|--level|-20")
    chanmap_cli_test(chanmap_cli_needs_confirm 2 "refusing to play without --confirm-loopback-only"
        "measure|--position|hp1")
    chanmap_cli_test(chanmap_cli_report_empty 2 "cannot read" "report|--out|/nonexistent/dir")
    set(rdir ${CMAKE_CURRENT_BINARY_DIR}/report-scratch)
    chanmap_cli_test(chanmap_cli_report_verified 0 "verdict: VERIFIED" "report|--out|${rdir}/verified")
    chanmap_cli_test(chanmap_cli_report_mismatch 1 "hp1 +1 +P8->C0\\+ P9->C0- +P8->C0- P9->C0\\+ +MISMATCH: left/right swapped"
        "report|--out|${rdir}/mismatch")
    chanmap_cli_test(chanmap_cli_report_incomplete 7 "verdict: INCOMPLETE" "report|--out|${rdir}/incomplete")
    set_tests_properties(chanmap_cli_report_verified chanmap_cli_report_mismatch
        chanmap_cli_report_incomplete PROPERTIES FIXTURES_REQUIRED chanmap_results)

    # The linked program: reads mixer controls, never writes or adds any,
    # and has no mixer API that could change a gain.
    add_test(NAME chanmap_binary_symbols
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DBINARY=$<TARGET_FILE:ff802-chanmap>
            "-DFORBIDDEN=snd_ctl_elem_write|snd_ctl_elem_tlv_write|snd_ctl_elem_tlv_command|snd_ctl_elem_add_integer|snd_ctl_elem_add_integer64|snd_ctl_elem_add_boolean|snd_ctl_elem_add_enumerated|snd_ctl_elem_add_bytes|snd_ctl_elem_add_iec958|snd_ctl_elem_remove|snd_ctl_elem_lock|snd_ctl_elem_unlock|snd_ctl_set_power_state|snd_hctl_elem_write|snd_mixer_open|snd_mixer_selem_set_playback_volume|snd_mixer_selem_set_playback_volume_all|snd_mixer_selem_set_capture_volume|snd_mixer_selem_set_playback_switch|snd_mixer_selem_set_capture_switch|snd_mixer_selem_set_playback_dB|snd_mixer_selem_set_capture_dB|snd_pcm_set_params|snd_use_case_set"
            "-DREQUIRED=snd_pcm_open|snd_pcm_writei|snd_pcm_readi|snd_ctl_card_info|snd_ctl_elem_read"
            -P ${PROJECT_SOURCE_DIR}/tests/check_binary_symbols.cmake)
else()
    ff802_alsa_test_unavailable(test_chanmap_alsa_null "alsa-lib not found")
    ff802_alsa_test_unavailable(chanmap_binary_symbols "alsa-lib not found")
endif()
