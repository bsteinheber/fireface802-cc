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
ff802_add_test(test_device_id ff802_detect)
ff802_add_test(test_detect ff802_detect)

# Tests using the simulated USB backend.
foreach(name test_usb_backend test_cc_switch)
    add_executable(${name} ${name}.c fake_usb.c)
    target_link_libraries(${name} PRIVATE ff802_switch)
    ff802_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endforeach()

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

    add_test(NAME cli_version COMMAND ff802-detect --version)
    set_tests_properties(cli_version PROPERTIES
        PASS_REGULAR_EXPRESSION "^ff802-detect ${PROJECT_VERSION}\n$")

    add_test(NAME cli_unknown_arg COMMAND ff802-detect --bogus)
    set_tests_properties(cli_unknown_arg PROPERTIES WILL_FAIL TRUE)
endif()
