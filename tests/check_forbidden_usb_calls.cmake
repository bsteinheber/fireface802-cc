# Static safety check over all sources.
#
# 1. Functions in ALWAYS_FORBIDDEN must not appear in any source file:
#    detaching kernel drivers, resets, configuration / alternate setting
#    changes, control/interrupt/asynchronous transfers, string descriptors.
# 2. Functions in COMM_ONLY (open, claim, release, bulk transfer, ...) may
#    appear only in src/usb_comm.c, the dedicated communication module.
# 3. The communication header ff802/usb_comm.h may only be included by the
#    communication module itself and by the ff802-cc tool. The detection
#    code (ff802-detect) therefore stays write-free.
#
# Usage: cmake -DSOURCE_DIR=<project root> -P check_forbidden_usb_calls.cmake

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not set")
endif()

set(ALWAYS_FORBIDDEN
    libusb_open_device_with_vid_pid
    libusb_wrap_sys_device
    libusb_detach_kernel_driver
    libusb_attach_kernel_driver
    libusb_set_auto_detach_kernel_driver
    libusb_reset_device
    libusb_set_configuration
    libusb_set_interface_alt_setting
    libusb_clear_halt
    libusb_control_transfer
    libusb_interrupt_transfer
    libusb_alloc_transfer
    libusb_submit_transfer
    libusb_get_string_descriptor
    libusb_get_string_descriptor_ascii
    libusb_get_descriptor
    libusb_get_configuration
    libusb_alloc_streams
    libusb_dev_mem_alloc
)

set(COMM_ONLY
    libusb_open
    libusb_close
    libusb_claim_interface
    libusb_release_interface
    libusb_bulk_transfer
    libusb_kernel_driver_active
    libusb_get_active_config_descriptor
)

set(COMM_FILE "${SOURCE_DIR}/src/usb_comm.c")
# ff802-cc disable only: claim with libusb_set_auto_detach_kernel_driver
# (hardware test 2026-11-19). The only file that may use it, and only
# together with libusb_claim_interface.
set(DETACH_FILE "${SOURCE_DIR}/src/usb_comm_detach.c")
set(DETACH_ALLOWED libusb_set_auto_detach_kernel_driver)
set(DETACH_FILE_COMM libusb_claim_interface)
set(COMM_HEADER_ALLOWED
    "${SOURCE_DIR}/src/usb_comm.c"
    "${SOURCE_DIR}/src/usb_comm_detach.c"
    "${SOURCE_DIR}/tools/ff802_cc.c"
    "${SOURCE_DIR}/tools/ff802_cc_autoswitch.c")

file(GLOB_RECURSE sources
    "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/include/*.h"
    "${SOURCE_DIR}/tools/*.c" "${SOURCE_DIR}/tools/*.h")

# sysfs USB control: only the hub port "disable" attribute, only in
# src/usb_port.c; no usbreset, no "authorized", no driver bind/unbind.
set(SYSFS_FORBIDDEN
    "/authorized"
    "\"usbreset"
    "USBDEVFS_RESET"
    "/unbind"
    "drivers/[A-Za-z0-9_-]+/bind"
)
set(PORT_FILE "${SOURCE_DIR}/src/usb_port.c")

set(violations 0)

function(report msg)
    message(SEND_ERROR "${msg}")
    math(EXPR v "${violations} + 1")
    set(violations ${v} PARENT_SCOPE)
endfunction()

foreach(src IN LISTS sources)
    file(READ "${src}" content)

    foreach(fn IN LISTS ALWAYS_FORBIDDEN)
        if(src STREQUAL DETACH_FILE AND fn IN_LIST DETACH_ALLOWED)
            continue()
        endif()
        if(content MATCHES "(^|[^A-Za-z0-9_])${fn}([^A-Za-z0-9_]|$)")
            report("forbidden USB call '${fn}' in ${src}")
        endif()
    endforeach()

    if(NOT src STREQUAL COMM_FILE)
        foreach(fn IN LISTS COMM_ONLY)
            if(src STREQUAL DETACH_FILE AND fn IN_LIST DETACH_FILE_COMM)
                continue()
            endif()
            if(content MATCHES "(^|[^A-Za-z0-9_])${fn}([^A-Za-z0-9_]|$)")
                report("'${fn}' is only allowed in src/usb_comm.c, found in ${src}")
            endif()
        endforeach()
    endif()

    foreach(pat IN LISTS SYSFS_FORBIDDEN)
        if(content MATCHES "${pat}")
            report("forbidden USB sysfs control '${pat}' in ${src}")
        endif()
    endforeach()
    if(NOT src STREQUAL PORT_FILE AND content MATCHES "/disable\"")
        report("the hub port 'disable' attribute may only be written in src/usb_port.c, found in ${src}")
    endif()

    if(content MATCHES "#[ \t]*include[ \t]*[<\"]ff802/usb_comm\\.h[>\"]")
        list(FIND COMM_HEADER_ALLOWED "${src}" idx)
        if(idx EQUAL -1)
            report("ff802/usb_comm.h must not be included by ${src}")
        endif()
    endif()
endforeach()

list(LENGTH sources nfiles)
if(violations GREATER 0)
    message(FATAL_ERROR "${violations} USB safety violation(s) found")
endif()
message(STATUS "checked ${nfiles} source files: no USB safety violations")
