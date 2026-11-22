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
    FF802_VERSION="${PROJECT_VERSION}"
    FF802_GIT_REVISION="${FF802_GIT_REVISION}"
    FF802_BUILD_TYPE="${FF802_BUILD_TYPE_NAME}")
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
    FF802_VERSION="${PROJECT_VERSION}"
    FF802_GIT_REVISION="${FF802_GIT_REVISION}"
    FF802_BUILD_TYPE="${FF802_BUILD_TYPE_NAME}")
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
# Mixer initialisation in class-compliant mode: ALSA sequencer backend
# (ff802_midi_alsa, subscribe/send only with FF802_ENABLE_MIXER_WRITE).
# Without alsa-lib the program is built without it and reports the mixer
# initialisation as not available; the Debian package requires it.
if(TARGET ff802_midi_alsa)
    target_link_libraries(ff802-cc-autoswitch PRIVATE ff802_midi_alsa)
    target_compile_definitions(ff802-cc-autoswitch PRIVATE FF802_HAVE_MIXER_SEQ=1)
    set_target_properties(ff802-cc-autoswitch PROPERTIES C_EXTENSIONS ON)
else()
    message(WARNING "alsa-lib not found: ff802-cc-autoswitch is built without "
                    "the mixer initialisation")
endif()

# Manual mixer initialisation (same flow and backend as the service).
# Installed by the ff802-cc-auto package as /usr/bin/ff802-mixer-init;
# sends only in a build with FF802_ENABLE_MIXER_WRITE.
if(TARGET ff802_midi_alsa)
    add_executable(ff802-mixer-init ff802_mixer_init.c)
    target_link_libraries(ff802-mixer-init PRIVATE ff802_auto ff802_midi_alsa)
    target_compile_definitions(ff802-mixer-init PRIVATE FF802_VERSION="${PROJECT_VERSION}")
    set_target_properties(ff802-mixer-init PROPERTIES
        C_EXTENSIONS ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    ff802_set_warnings(ff802-mixer-init)
endif()
