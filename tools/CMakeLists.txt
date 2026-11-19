# Read-only libusb enumeration backend.
add_library(ff802_usb STATIC
    ${PROJECT_SOURCE_DIR}/src/usb_enum.c)
target_include_directories(ff802_usb PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ff802_usb
    PUBLIC ff802_detect PkgConfig::LIBUSB)
ff802_set_warnings(ff802_usb)

# Diagnostic tool. Placed directly in the build directory for easy access.
# Must stay write-free: it does not link the communication module.
add_executable(ff802-detect ff802_detect.c)
target_link_libraries(ff802-detect PRIVATE ff802_usb ff802_detect)
target_compile_definitions(ff802-detect PRIVATE
    FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-detect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
ff802_set_warnings(ff802-detect)

# USB communication backend: the only module allowed to open, claim and
# write. Claim and write are compiled in only with FF802_ENABLE_USB_WRITE.
add_library(ff802_comm STATIC
    ${PROJECT_SOURCE_DIR}/src/usb_comm.c
    ${PROJECT_SOURCE_DIR}/src/usb_comm_detach.c)
target_include_directories(ff802_comm PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ff802_comm
    PUBLIC ff802_switch
    PRIVATE ff802_usb PkgConfig::LIBUSB)
if(FF802_ENABLE_USB_WRITE)
    target_compile_definitions(ff802_comm PRIVATE FF802_ENABLE_USB_WRITE=1)
else()
    target_compile_definitions(ff802_comm PRIVATE FF802_ENABLE_USB_WRITE=0)
endif()
ff802_set_warnings(ff802_comm)

# Mode switch tool.
add_executable(ff802-cc ff802_cc.c)
target_link_libraries(ff802-cc PRIVATE ff802_comm ff802_switch ff802_auto)
target_compile_definitions(ff802-cc PRIVATE
    FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-cc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
ff802_set_warnings(ff802-cc)

install(TARGETS ff802-detect ff802-cc RUNTIME DESTINATION bin)

# T4: non-interactive CC enable for one USB connection, started only by
# ff802-cc-auto@.service. Installed in /usr/libexec/ff802-cc by the
# ff802-cc-auto package (built there with FF802_ENABLE_USB_WRITE=ON).
add_executable(ff802-cc-autoswitch ff802_cc_autoswitch.c)
target_link_libraries(ff802-cc-autoswitch PRIVATE ff802_auto ff802_comm)
target_compile_definitions(ff802-cc-autoswitch PRIVATE
    FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-cc-autoswitch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
ff802_set_warnings(ff802-cc-autoswitch)
