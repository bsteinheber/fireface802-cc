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

# udev rule and systemd units: static checks and systemd-analyze verify.
add_test(NAME auto_units
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/check_auto_units.sh
            ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/units-scratch)
