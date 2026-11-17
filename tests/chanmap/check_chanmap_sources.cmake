# Static checks over the ff802-chanmap sources:
#  - snd_pcm_open and snd_ctl_open only in src/chanmap_alsa.c (and the
#    read-only 'check' command of the tool for snd_ctl_open)
#  - no device names other than hw:CARD=<id>: no "default", "plughw",
#    "plug:", "pulse", "pipewire", "sysdefault", "dmix", "dsnoop", "front"
#  - the level constants: -40 dBFS bound, amplitude below it
#
# Usage: cmake -DSOURCE_DIR=<repo> -P check_chanmap_sources.cmake

set(errors 0)
macro(fail msg)
    message(SEND_ERROR "${msg}")
    math(EXPR errors "${errors} + 1")
endmacro()

file(GLOB srcs ${SOURCE_DIR}/src/chanmap_*.c ${SOURCE_DIR}/tools/ff802_chanmap.c)
list(LENGTH srcs n)
if(n LESS 7)
    message(FATAL_ERROR "expected 7 chanmap sources, found ${n}")
endif()

foreach(f IN LISTS srcs)
    file(READ ${f} text)
    # strip comments
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" code "${text}")
    get_filename_component(name ${f} NAME)
    if(code MATCHES "snd_pcm_open" AND NOT name STREQUAL "chanmap_alsa.c")
        fail("${name}: snd_pcm_open outside chanmap_alsa.c")
    endif()
    if(code MATCHES "snd_ctl_open" AND NOT name MATCHES "^(chanmap_alsa.c|ff802_chanmap.c)$")
        fail("${name}: snd_ctl_open outside chanmap_alsa.c / ff802_chanmap.c")
    endif()
    foreach(dev "\"default" "plughw" "\"plug:" "\"pulse" "\"pipewire" "sysdefault" "\"dmix" "\"dsnoop" "\"front")
        string(FIND "${code}" "${dev}" pos)
        if(NOT pos EQUAL -1)
            fail("${name}: device name ${dev} must not appear")
        endif()
    endforeach()
endforeach()

file(READ ${SOURCE_DIR}/src/chanmap_alsa.c alsa)
if(NOT alsa MATCHES "\"hw:CARD=%s,DEV=0\"")
    fail("chanmap_alsa.c: PCM name is not hw:CARD=<id>,DEV=0")
endif()

file(READ ${SOURCE_DIR}/include/ff802/chanmap.h hdr)
if(NOT hdr MATCHES "#define CHANMAP_MAX_PEAK +0\\.01\n")
    fail("CHANMAP_MAX_PEAK must be 0.01 (-40 dBFS)")
endif()
if(NOT hdr MATCHES "#define CHANMAP_AMPLITUDE +0\\.0099\n")
    fail("CHANMAP_AMPLITUDE must be 0.0099")
endif()
if(hdr MATCHES "CHANMAP_AMPLITUDE[^\n]*getenv|level_option|--level")
    fail("the level must not be configurable")
endif()

if(errors)
    message(FATAL_ERROR "${errors} chanmap source check(s) failed")
endif()
message(STATUS "chanmap sources: ${n} files checked")
