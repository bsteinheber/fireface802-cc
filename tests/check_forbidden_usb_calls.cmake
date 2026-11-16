# Safety check: fail if any source file uses a libusb API that opens a
# device, claims an interface, detaches a kernel driver, resets a device,
# changes configuration or performs a USB transfer.
#
# Usage: cmake -DSOURCE_DIR=<project root> -P check_forbidden_usb_calls.cmake

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not set")
endif()

set(forbidden
    libusb_open
    libusb_open_device_with_vid_pid
    libusb_wrap_sys_device
    libusb_claim_interface
    libusb_release_interface
    libusb_detach_kernel_driver
    libusb_attach_kernel_driver
    libusb_set_auto_detach_kernel_driver
    libusb_reset_device
    libusb_set_configuration
    libusb_set_interface_alt_setting
    libusb_clear_halt
    libusb_control_transfer
    libusb_bulk_transfer
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

file(GLOB_RECURSE sources
    "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/include/*.h"
    "${SOURCE_DIR}/tools/*.c" "${SOURCE_DIR}/tools/*.h")

set(violations 0)
foreach(src IN LISTS sources)
    file(READ "${src}" content)
    foreach(fn IN LISTS forbidden)
        # Match the identifier as a whole word.
        if(content MATCHES "(^|[^A-Za-z0-9_])${fn}([^A-Za-z0-9_]|$)")
            message(SEND_ERROR "forbidden USB call '${fn}' in ${src}")
            math(EXPR violations "${violations} + 1")
        endif()
    endforeach()
endforeach()

list(LENGTH sources nfiles)
if(violations GREATER 0)
    message(FATAL_ERROR "${violations} forbidden USB call(s) found")
endif()
message(STATUS "checked ${nfiles} source files: no forbidden USB calls")
