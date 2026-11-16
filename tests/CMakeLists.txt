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
