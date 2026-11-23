# T4 tests: automatic switch to class-compliant mode.
#
# Everything runs against a fake sysfs tree and the simulated USB backend
# in private temporary directories. No test links libusb or opens a
# device node.

add_executable(test_auto_sysfs test_auto_sysfs.c)
target_include_directories(test_auto_sysfs PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(test_auto_sysfs PRIVATE ff802_auto)
set_target_properties(test_auto_sysfs PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(test_auto_sysfs)
add_test(NAME test_auto_sysfs COMMAND test_auto_sysfs)

# Mixer initialisation: sound card of a connection, sequencer port.
add_executable(test_mixer_port test_mixer_port.c)
target_include_directories(test_mixer_port PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(test_mixer_port PRIVATE ff802_auto)
set_target_properties(test_mixer_port PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(test_mixer_port)
add_test(NAME test_mixer_port COMMAND test_mixer_port)

add_executable(test_mixer_run test_mixer_run.c)
target_include_directories(test_mixer_run PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(test_mixer_run PRIVATE ff802_auto)
set_target_properties(test_mixer_run PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(test_mixer_run)
add_test(NAME test_mixer_run COMMAND test_mixer_run)

# udev rule and systemd units: static checks and systemd-analyze verify.
add_test(NAME auto_units
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/check_auto_units.sh
            ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/units-scratch)

# The switch flow against fake sysfs + tests/fake_usb.c.
add_executable(test_auto_switch test_auto_switch.c ${PROJECT_SOURCE_DIR}/tests/fake_usb.c)
target_include_directories(test_auto_switch PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(test_auto_switch PRIVATE ff802_auto)
set_target_properties(test_auto_switch PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(test_auto_switch)
add_test(NAME test_auto_switch COMMAND test_auto_switch)

# Test build of ff802-cc-autoswitch: same main program, simulated backend,
# no libusb; works only below $FF802_AUTO_ROOT. Never installed.
add_executable(ff802-cc-autoswitch-sim
    ${PROJECT_SOURCE_DIR}/tools/ff802_cc_autoswitch.c sim_usb.c sim_midi.c)
target_include_directories(ff802-cc-autoswitch-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ff802-cc-autoswitch-sim PRIVATE ff802_auto)
target_compile_definitions(ff802-cc-autoswitch-sim PRIVATE
    FF802_AUTO_SIM=1 FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-cc-autoswitch-sim PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(ff802-cc-autoswitch-sim)

# Service level: ExecStart of the unit and the instance of the udev rule,
# run with the simulation build (single, repeated and simultaneous events).
if(FF802_HAVE_TOOLS)
    add_test(NAME auto_flow
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_auto_flow.sh
                ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-cc-autoswitch-sim>
                $<TARGET_FILE:ff802-cc-autoswitch>
                ${CMAKE_CURRENT_BINARY_DIR}/flow-scratch)

    # The simulation build must not contain any libusb or ALSA call.
    add_test(NAME autoswitch_sim_no_libusb
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM}
            -DBINARY=$<TARGET_FILE:ff802-cc-autoswitch-sim>
            "-DFORBIDDEN=libusb_init|libusb_exit|libusb_get_device_list|libusb_open|libusb_close|libusb_claim_interface|libusb_release_interface|libusb_bulk_transfer|libusb_kernel_driver_active|libusb_get_active_config_descriptor|snd_seq_open|snd_seq_connect_to|snd_seq_event_output_direct|snd_rawmidi_open"
            "-DREQUIRED=flock"
            -P ${PROJECT_SOURCE_DIR}/tests/check_binary_symbols.cmake)
endif()

# Hardware test program B.2 with the simulated MIDI backend (no
# libasound): detection, the two messages, confirmation, all refusals.
add_executable(ff802-mixer-b2-sim
    ${PROJECT_SOURCE_DIR}/tools/hw-test/ff802_mixer_b2.c sim_midi.c)
target_include_directories(ff802-mixer-b2-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ff802-mixer-b2-sim PRIVATE ff802_auto)
target_compile_definitions(ff802-mixer-b2-sim PRIVATE
    FF802_B2_SIM=1 FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-mixer-b2-sim PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(ff802-mixer-b2-sim)
if(FF802_HAVE_MIXER_B2)
    set(b2_real $<TARGET_FILE:ff802-mixer-b2>)
else()
    set(b2_real "-")
endif()
add_test(NAME mixer_b2
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_mixer_b2.sh
            ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-mixer-b2-sim> ${b2_real}
            ${CMAKE_NM} ${CMAKE_CURRENT_BINARY_DIR}/b2-scratch)

# Hardware test program B.3 (one routing write), same scheme as B.2.
add_executable(ff802-mixer-b3-sim
    ${PROJECT_SOURCE_DIR}/tools/hw-test/ff802_mixer_b3.c sim_midi.c)
target_include_directories(ff802-mixer-b3-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ff802-mixer-b3-sim PRIVATE ff802_auto)
target_compile_definitions(ff802-mixer-b3-sim PRIVATE
    FF802_B3_SIM=1 FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-mixer-b3-sim PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(ff802-mixer-b3-sim)
if(FF802_HAVE_MIXER_B3)
    set(b3_real $<TARGET_FILE:ff802-mixer-b3>)
else()
    set(b3_real "-")
endif()
add_test(NAME mixer_b3
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_mixer_b3.sh
            ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-mixer-b3-sim> ${b3_real}
            ${CMAKE_NM} ${CMAKE_CURRENT_BINARY_DIR}/b3-scratch)

# Hardware test program B.4 (one routing write, off), same scheme as B.3.
add_executable(ff802-mixer-b4-sim
    ${PROJECT_SOURCE_DIR}/tools/hw-test/ff802_mixer_b4.c sim_midi.c)
target_include_directories(ff802-mixer-b4-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ff802-mixer-b4-sim PRIVATE ff802_auto)
target_compile_definitions(ff802-mixer-b4-sim PRIVATE
    FF802_B4_SIM=1 FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-mixer-b4-sim PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(ff802-mixer-b4-sim)
if(FF802_HAVE_MIXER_B4)
    set(b4_real $<TARGET_FILE:ff802-mixer-b4>)
else()
    set(b4_real "-")
endif()
add_test(NAME mixer_b4
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_mixer_b4.sh
            ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-mixer-b4-sim> ${b4_real}
            ${CMAKE_NM} ${CMAKE_CURRENT_BINARY_DIR}/b4-scratch)

# Diagnosis D1 (one routing write, off or 0 dB), same scheme as B.4.
add_executable(ff802-mixer-d1-sim
    ${PROJECT_SOURCE_DIR}/tools/hw-test/ff802_mixer_d1.c sim_midi.c)
target_include_directories(ff802-mixer-d1-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ff802-mixer-d1-sim PRIVATE ff802_auto)
target_compile_definitions(ff802-mixer-d1-sim PRIVATE
    FF802_D1_SIM=1 FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-mixer-d1-sim PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(ff802-mixer-d1-sim)
if(FF802_HAVE_MIXER_D1)
    set(d1_real $<TARGET_FILE:ff802-mixer-d1>)
else()
    set(d1_real "-")
endif()
add_test(NAME mixer_d1
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_mixer_d1.sh
            ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-mixer-d1-sim> ${d1_real}
            ${CMAKE_NM} ${CMAKE_CURRENT_BINARY_DIR}/d1-scratch)

# Manual mixer initialisation with the simulated MIDI backend.
add_executable(ff802-mixer-init-sim
    ${PROJECT_SOURCE_DIR}/tools/ff802_mixer_init.c sim_midi.c)
target_include_directories(ff802-mixer-init-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ff802-mixer-init-sim PRIVATE ff802_auto)
target_compile_definitions(ff802-mixer-init-sim PRIVATE
    FF802_MIXER_INIT_SIM=1 FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-mixer-init-sim PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(ff802-mixer-init-sim)
if(TARGET ff802-mixer-init)
    set(mi_real $<TARGET_FILE:ff802-mixer-init>)
else()
    set(mi_real "-")
endif()
add_test(NAME mixer_init_tool
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_mixer_init_tool.sh
            ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-mixer-init-sim> ${mi_real}
            ${CMAKE_NM} ${CMAKE_CURRENT_BINARY_DIR}/mi-scratch)

# Desktop notification (test doubles for notify-send and busctl) and its
# decoupling from the system service.
add_test(NAME auto_notify
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_auto_notify.sh
            ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-cc-autoswitch-sim>
            ${CMAKE_CURRENT_BINARY_DIR}/notify-scratch)

# ff802-cc-auto on/off/status, root helpers, polkit policy.
add_test(NAME auto_control
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_auto_control.sh
            ${PROJECT_SOURCE_DIR} $<TARGET_FILE:ff802-cc-autoswitch-sim>
            ${CMAKE_CURRENT_BINARY_DIR}/control-scratch)
