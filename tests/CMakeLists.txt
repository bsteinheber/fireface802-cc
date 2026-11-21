# Test executables are registered with CTest below.
# Each test is a plain C program that returns 0 on success.

function(ff802_add_test name lib)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE ${lib})
    ff802_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ff802_add_test(test_reg_codec ff802_protocol)
ff802_add_test(test_cc_mode ff802_protocol)
ff802_add_test(test_mixer_init ff802_protocol)
ff802_add_test(test_device_id ff802_detect)
ff802_add_test(test_detect ff802_detect)

# Hub port of a device (fake sysfs tree of tests/auto).
add_executable(test_usb_port test_usb_port.c)
target_include_directories(test_usb_port PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/auto)
target_link_libraries(test_usb_port PRIVATE ff802_auto)
set_target_properties(test_usb_port PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(test_usb_port)
add_test(NAME test_usb_port COMMAND test_usb_port)

# Preconditions of ff802-cc disable (fake sysfs, /proc/asound, T4 state).
add_executable(test_disable_check test_disable_check.c)
target_include_directories(test_disable_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/auto)
target_link_libraries(test_disable_check PRIVATE ff802_auto)
set_target_properties(test_disable_check PROPERTIES C_EXTENSIONS ON)
ff802_set_warnings(test_disable_check)
add_test(NAME test_disable_check COMMAND test_disable_check)

# Tests using the simulated USB backend.
foreach(name test_usb_backend test_cc_switch test_cc_disable)
    add_executable(${name} ${name}.c fake_usb.c)
    target_link_libraries(${name} PRIVATE ff802_switch)
    ff802_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endforeach()

# Release build with -Werror twice in different directories: identical
# programs (reproducible build).
add_test(NAME reproducible_release_build
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/check_reproducible_build.sh
            ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/repro-scratch)
set_tests_properties(reproducible_release_build PROPERTIES TIMEOUT 900)

# Static safety check over all sources (no compiler involved).
add_test(NAME check_forbidden_usb_calls
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_forbidden_usb_calls.cmake)

# Command-line behaviour that does not touch USB: argument handling
# happens before any enumeration.
if(FF802_HAVE_TOOLS)
    add_test(NAME cli_help COMMAND ff802-detect --help)
    add_test(NAME cli_help_text COMMAND ff802-detect --help)
    set_tests_properties(cli_help_text PROPERTIES
        PASS_REGULAR_EXPRESSION "Usage: ff802-detect")

    # --version: project version, Git revision of the configuration, build type
    string(REPLACE "+" "\\+" rev_re "${FF802_GIT_REVISION}")
    add_test(NAME cli_version COMMAND ff802-detect --version)
    set_tests_properties(cli_version PROPERTIES
        PASS_REGULAR_EXPRESSION
        "^ff802-detect ${PROJECT_VERSION} \\(git ${rev_re}, ${FF802_BUILD_TYPE_NAME}\\)\n$")
    if(NOT FF802_GIT_REVISION STREQUAL "unknown")
        add_test(NAME cli_version_revision_format
            COMMAND sh -c "\"$0\" --version | grep -Eqx 'ff802-cc [0-9]+\\.[0-9]+\\.[0-9]+ \\(git [0-9a-f]{12}(\\+dirty)?, [A-Za-z]+, USB writes (enabled|disabled)\\)'"
                    $<TARGET_FILE:ff802-cc>)
    endif()

    add_test(NAME cli_unknown_arg COMMAND ff802-detect --bogus)
    set_tests_properties(cli_unknown_arg PROPERTIES WILL_FAIL TRUE)

    # The real backend reports the configured write capability.
    if(FF802_ENABLE_USB_WRITE)
        set(expect_write 1)
        set(write_word enabled)
    else()
        set(expect_write 0)
        set(write_word disabled)
    endif()
    add_executable(test_write_gate test_write_gate.c)
    target_link_libraries(test_write_gate PRIVATE ff802_comm)
    target_compile_definitions(test_write_gate PRIVATE
        FF802_EXPECT_WRITE_ENABLED=${expect_write})
    ff802_set_warnings(test_write_gate)
    add_test(NAME test_write_gate COMMAND test_write_gate)

    # ff802-cc command line, checked with exact exit codes. None of these
    # reach the USB layer: arguments are validated first, and 'enable'
    # refuses to run without an interactive terminal (stdin is /dev/null).
    function(ff802_cli_test name rc regex)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCMD=$<TARGET_FILE:ff802-cc> "-DARGS=${ARGN}"
                -DEXPECT_RC=${rc} "-DEXPECT_REGEX=${regex}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_expect.cmake)
    endfunction()
    ff802_cli_test(cc_cli_help 0 "Usage: ff802-cc" "--help")
    ff802_cli_test(cc_cli_version 0
        "^ff802-cc ${PROJECT_VERSION} \\(git ${rev_re}, ${FF802_BUILD_TYPE_NAME}, USB writes ${write_word}\\)\n$"
        "--version")
    ff802_cli_test(cc_cli_no_command 2 "Usage: ff802-cc")
    ff802_cli_test(cc_cli_unknown_command 2 "unexpected argument 'switch'" "switch")
    ff802_cli_test(cc_cli_two_commands 2 "unexpected argument" "check|enable")
    # disable refuses without an interactive terminal, before reading
    # sysfs or touching USB (stdin is /dev/null)
    ff802_cli_test(cc_cli_disable 2 "'disable' requires an interactive terminal" "disable")
    # --no-port-reset belongs to enable; enable refuses without a terminal
    # before the port check
    ff802_cli_test(cc_cli_no_port_reset_check 2 "only valid with 'enable'"
        "--no-port-reset|check")
    ff802_cli_test(cc_cli_no_port_reset_disable 2 "only valid with 'enable'"
        "--no-port-reset|disable")
    ff802_cli_test(cc_cli_enable_port_reset_needs_tty 2 "requires an interactive terminal"
        "--no-port-reset|enable")
    ff802_cli_test(cc_cli_disable_device 2 "'disable' requires an interactive terminal"
        "--device|3:19|disable")
    ff802_cli_test(cc_cli_disable_help 0 "disable Switch a unit in class-compliant mode" "--help")
    ff802_cli_test(cc_cli_disable_twice 2 "unexpected argument" "disable|disable")
    ff802_cli_test(cc_cli_device_missing 2 "--device expects" "--device")
    ff802_cli_test(cc_cli_device_invalid 2 "--device expects" "--device|3-18|check")
    ff802_cli_test(cc_cli_enable_needs_tty 2 "requires an interactive terminal" "enable")
    ff802_cli_test(cc_cli_enable_device_needs_tty 2 "requires an interactive terminal"
        "--device|3:18|enable")

    # Inspect the linked binaries (independent of the sources).
    set(always_forbidden
        "libusb_detach_kernel_driver|libusb_set_auto_detach_kernel_driver|libusb_reset_device|libusb_set_configuration|libusb_set_interface_alt_setting|libusb_clear_halt|libusb_control_transfer|libusb_interrupt_transfer|libusb_submit_transfer|libusb_open_device_with_vid_pid")
    set(write_symbols
        "libusb_claim_interface|libusb_release_interface|libusb_bulk_transfer")

    # ff802-detect must not even be able to open a device.
    add_test(NAME detect_binary_symbols
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DBINARY=$<TARGET_FILE:ff802-detect>
            "-DFORBIDDEN=${always_forbidden}|libusb_open|${write_symbols}"
            "-DREQUIRED=libusb_get_device_list"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_binary_symbols.cmake)

    if(FF802_ENABLE_USB_WRITE)
        # ff802-cc disable needs libusb auto-detach (only that one detach
        # function; libusb_detach_kernel_driver and reset stay forbidden).
        string(REPLACE "|libusb_set_auto_detach_kernel_driver" "" cc_forbidden
               "${always_forbidden}")
        add_test(NAME cc_binary_symbols
            COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DBINARY=$<TARGET_FILE:ff802-cc>
                "-DFORBIDDEN=${cc_forbidden}"
                "-DREQUIRED=libusb_open|${write_symbols}|libusb_set_auto_detach_kernel_driver"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_binary_symbols.cmake)
    else()
        # Write gate closed: claim, release and bulk transfer are absent.
        add_test(NAME cc_binary_symbols
            COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DBINARY=$<TARGET_FILE:ff802-cc>
                "-DFORBIDDEN=${always_forbidden}|${write_symbols}"
                "-DREQUIRED=libusb_open|libusb_kernel_driver_active"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_binary_symbols.cmake)
    endif()

    # T4 program: same rules as ff802-cc.
    if(FF802_ENABLE_USB_WRITE)
        add_test(NAME autoswitch_binary_symbols
            COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DBINARY=$<TARGET_FILE:ff802-cc-autoswitch>
                "-DFORBIDDEN=${always_forbidden}"
                "-DREQUIRED=libusb_open|${write_symbols}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_binary_symbols.cmake)
    else()
        add_test(NAME autoswitch_binary_symbols
            COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DBINARY=$<TARGET_FILE:ff802-cc-autoswitch>
                "-DFORBIDDEN=${always_forbidden}|${write_symbols}"
                "-DREQUIRED=libusb_open|libusb_kernel_driver_active"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_binary_symbols.cmake)
    endif()
endif()

# Static checks of the ALSA UCM profile (no sound card needed).
add_subdirectory(audio)

# ff802-chanmap (loopback measurement, hardware test T2).
add_subdirectory(chanmap)

# Debian package of the UCM profile (isolated install/remove test).
add_subdirectory(packaging)

# T4: automatic CC mode switch (simulated sysfs, USB and desktop only).
add_subdirectory(auto)
