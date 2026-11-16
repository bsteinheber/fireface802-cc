# Read-only libusb enumeration backend.
add_library(ff802_usb STATIC
    ${PROJECT_SOURCE_DIR}/src/usb_enum.c)
target_link_libraries(ff802_usb
    PUBLIC ff802_detect
    PRIVATE PkgConfig::LIBUSB)
ff802_set_warnings(ff802_usb)

# Diagnostic tool. Placed directly in the build directory for easy access.
add_executable(ff802-detect ff802_detect.c)
target_link_libraries(ff802-detect PRIVATE ff802_usb ff802_detect)
target_compile_definitions(ff802-detect PRIVATE
    FF802_VERSION="${PROJECT_VERSION}")
set_target_properties(ff802-detect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
ff802_set_warnings(ff802-detect)

install(TARGETS ff802-detect RUNTIME DESTINATION bin)
