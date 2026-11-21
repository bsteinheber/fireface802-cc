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
    ${PROJECT_SOURCE_DIR}/tools/ff802_cc_autoswitch.c sim_usb.c)
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

    # The simulation build must not contain any libusb call.
    add_test(NAME autoswitch_sim_no_libusb
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM}
            -DBINARY=$<TARGET_FILE:ff802-cc-autoswitch-sim>
            "-DFORBIDDEN=libusb_init|libusb_exit|libusb_get_device_list|libusb_open|libusb_close|libusb_claim_interface|libusb_release_interface|libusb_bulk_transfer|libusb_kernel_driver_active|libusb_get_active_config_descriptor"
            "-DREQUIRED=flock"
            -P ${PROJECT_SOURCE_DIR}/tests/check_binary_symbols.cmake)
endif()

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
