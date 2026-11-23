# Static safety check of the mixer initialisation over all program sources
# (src, tools, include; not the tests).
#
# 1. ALSA sequencer, rawmidi and control functions (snd_seq_, snd_rawmidi_,
#    snd_ctl_elem_write) appear only in src/midi_seq_alsa.c.
# 2. The RME SysEx header (manufacturer 00 20 0D) is built only by
#    src/cc_mode.c (CC disable), src/mixer_init.c (mixer registers) and
#    the hardware tests tools/hw-test/ff802_mixer_b2.c, _b3.c, _b4.c, _d1.c, _d3.c (their
#    fixed messages).
# 3. ff802_rme_sysex_reg() is called only by src/mixer_run.c (and defined
#    in src/mixer_init.c).
# 4. The mixer register range 0x4000.. (matrix) and the output volume
#    registers appear as literals only in src/mixer_init.c and the
#    hardware tests tools/hw-test/ff802_mixer_b2.c (register 0x2600) and
#    ff802_mixer_b3.c (registers 0x4228, 0x4269), ff802_mixer_b4.c (0x4261), ff802_mixer_d1.c (0x4269), ff802_mixer_d3.c (0x4220).
#
# Usage: cmake -DSOURCE_DIR=<project root> -P check_mixer_sources.cmake

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not set")
endif()

file(GLOB_RECURSE sources
    "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/tools/*.c" "${SOURCE_DIR}/include/*.h")

set(failed 0)
function(only_in pattern allowed what)
    foreach(f IN LISTS sources)
        file(READ "${f}" text)
        string(REGEX MATCH "${pattern}" hit "${text}")
        if(hit)
            list(FIND allowed "${f}" idx)
            if(idx EQUAL -1)
                message(SEND_ERROR "${f}: ${what} ('${hit}') outside the allowed files")
                set(failed 1 PARENT_SCOPE)
            endif()
        endif()
    endforeach()
endfunction()

only_in("snd_seq_[a-z_]+|snd_rawmidi_[a-z_]+|snd_ctl_elem_write"
        "${SOURCE_DIR}/src/midi_seq_alsa.c" "ALSA MIDI/control call")
only_in("0x00, 0x20, 0x0D"
        "${SOURCE_DIR}/src/cc_mode.c;${SOURCE_DIR}/src/mixer_init.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_b2.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_b3.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_b4.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_d1.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_d3.c"
        "RME SysEx header")
only_in("ff802_rme_sysex_reg\\("
        "${SOURCE_DIR}/src/mixer_init.c;${SOURCE_DIR}/src/mixer_run.c;${SOURCE_DIR}/include/ff802/mixer_init.h"
        "SysEx register write")
only_in("0x4[0-3][0-9A-Fa-f][0-9A-Fa-f][^0-9A-Fa-f]|0x(1[EF]|2[0-9])00[^0-9A-Fa-f]"
        "${SOURCE_DIR}/src/mixer_init.c;${SOURCE_DIR}/include/ff802/mixer_init.h;${SOURCE_DIR}/tools/hw-test/ff802_mixer_b2.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_b3.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_b4.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_d1.c;${SOURCE_DIR}/tools/hw-test/ff802_mixer_d3.c"
        "mixer register literal")

if(failed)
    message(FATAL_ERROR "mixer source check failed")
endif()
list(LENGTH sources n)
message(STATUS "mixer source check passed (${n} files scanned)")
