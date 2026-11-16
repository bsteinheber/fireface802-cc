# Static checks for the Fireface 802 ALSA UCM profile.
#
# Verifies structure and content of the UCM files against an expected
# device table that is kept here, independent of the configuration:
# device names, directions, channel indices, priorities, SplitPCM
# definitions, cross-file references and absence of directives that would
# write ALSA controls or restrict sample rates.
#
# Usage: cmake -DUCM_DIR=<dir containing USB-Audio/> -P check_ucm_profile.cmake

if(NOT UCM_DIR)
    message(FATAL_ERROR "UCM_DIR not set")
endif()

set(errors 0)
macro(fail msg)
    message(SEND_ERROR "${msg}")
    math(EXPR errors "${errors} + 1")
endmacro()

set(CONFD_FILE "${UCM_DIR}/USB-Audio/conf.d/0424-3fdd.conf")
set(MAIN_FILE  "${UCM_DIR}/USB-Audio/RME/Fireface-802.conf")
set(HIFI_FILE  "${UCM_DIR}/USB-Audio/RME/Fireface-802-HiFi.conf")

foreach(f CONFD_FILE MAIN_FILE HIFI_FILE)
    if(NOT EXISTS "${${f}}")
        message(FATAL_ERROR "missing file: ${${f}}")
    endif()
endforeach()

# Read a file and strip comments (our files never use '#' inside strings).
# The whole text is processed at once: line lists would be split wrongly by
# CMake because of the unbalanced '[' in "Macro [".
function(read_stripped path out)
    file(READ "${path}" content)
    string(REGEX REPLACE "#[^\n]*" "" content "${content}")
    set(${out} "${content}" PARENT_SCOPE)
endfunction()

file(READ "${CONFD_FILE}" confd_raw)
file(READ "${MAIN_FILE}" main_raw)
file(READ "${HIFI_FILE}" hifi_raw)
read_stripped("${CONFD_FILE}" confd)
read_stripped("${MAIN_FILE}" main)
read_stripped("${HIFI_FILE}" hifi)

# ---------------------------------------------------------------------------
# Safety: no ALSA control writes, no command execution, no hardware mixer,
# no rate restriction, no device conflicts, no known key typos.
set(forbidden_keywords
    cset cset-new cset-tlv cdev exec shell sysw
    BootSequence FixedBootSequence EnableSequence DisableSequence
    TransitionSequence SectionDefaults ValueDefaults
    PlaybackVolume CaptureVolume PlaybackSwitch CaptureSwitch
    PlaybackMixerElem CaptureMixerElem PlaybackMasterElem CaptureMasterElem
    PlaybackRate CaptureRate
    ConflictingDevice SupportedDevice
    PlackbackPriority)
foreach(name confd main hifi)
    foreach(kw IN LISTS forbidden_keywords)
        if(${name} MATCHES "(^|[^A-Za-z0-9_-])${kw}([^A-Za-z0-9_-]|$)")
            fail("forbidden keyword '${kw}' in ${name} file")
        endif()
    endforeach()
    if(${name} MATCHES ";")
        fail("unexpected ';' in ${name} file")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Channel map marker must be present and identical in both RME files.
foreach(name main_raw hifi_raw)
    if(NOT ${name} MATCHES "FF802-CHANNEL-MAP: (UNVERIFIED|VERIFIED)")
        fail("channel map marker missing in ${name}")
    endif()
    set(marker_${name} "${CMAKE_MATCH_1}")
endforeach()
if(NOT marker_main_raw STREQUAL marker_hifi_raw)
    fail("channel map markers differ: ${marker_main_raw} vs ${marker_hifi_raw}")
endif()
message(STATUS "channel map status: ${marker_main_raw}")

# ---------------------------------------------------------------------------
# conf.d hook selects the profile only for the original Fireface 802.
if(NOT confd MATCHES "Define\\.ProfileName \"RME/Fireface-802\"")
    fail("conf.d file does not define ProfileName RME/Fireface-802")
endif()
if(NOT confd MATCHES "Needle \"Fireface 802\"")
    fail("conf.d file does not check for 'Fireface 802'")
endif()
if(NOT confd MATCHES "Needle \"802 FS\"[^}]*}[^F]*False\\.Define\\.ProfileName")
    fail("conf.d file must only define ProfileName if '802 FS' is absent")
endif()
if(confd MATCHES "True\\.Define\\.ProfileName")
    fail("conf.d file must not define ProfileName in a True branch")
endif()

# Main file references the HiFi file, which must exist in the same tree.
if(NOT main MATCHES "SectionUseCase\\.\"HiFi\"")
    fail("main file has no HiFi use case")
endif()
if(NOT main MATCHES "File \"/USB-Audio/RME/Fireface-802-HiFi\\.conf\"")
    fail("main file does not reference /USB-Audio/RME/Fireface-802-HiFi.conf")
endif()
string(REGEX MATCHALL "SectionUseCase\\." usecases "${main}")
list(LENGTH usecases n_usecases)
if(NOT n_usecases EQUAL 1)
    fail("expected exactly one use case, found ${n_usecases}")
endif()
if(main MATCHES "DirectUseCase|directm\\.conf")
    fail("no Direct use case is wanted")
endif()

# ---------------------------------------------------------------------------
# SplitPCM definitions in the HiFi preamble.
if(NOT hifi MATCHES "^[ \t\n]*Include\\.pcm_split\\.File \"/common/pcm/split\\.conf\"")
    fail("HiFi file must start by including /common/pcm/split.conf")
endif()

string(REPLACE "SectionDevice." ";SectionDevice." parts "${hifi}")
list(POP_FRONT parts preamble)

foreach(pcm ff802_stereo_out:Playback ff802_stereo_in:Capture)
    string(REPLACE ":" ";" pcm "${pcm}")
    list(GET pcm 0 pcm_name)
    list(GET pcm 1 pcm_dir)
    string(REGEX MATCH "SplitPCM {[^}]*Name \"${pcm_name}\"[^}]*}" block "${preamble}")
    if(block STREQUAL "")
        fail("SplitPCM definition for ${pcm_name} missing")
        continue()
    endif()
    foreach(re "Direction ${pcm_dir}" "Format S24_3LE" "Channels 2" "HWChannels 22")
        if(NOT block MATCHES "${re}")
            fail("SplitPCM ${pcm_name}: expected '${re}'")
        endif()
    endforeach()
    string(REGEX MATCHALL "HWChannelPos[0-9]+" positions "${block}")
    list(LENGTH positions n_pos)
    if(NOT n_pos EQUAL 22)
        fail("SplitPCM ${pcm_name}: expected 22 HWChannelPos entries, found ${n_pos}")
    endif()
    foreach(i RANGE 0 21)
        if(NOT block MATCHES "HWChannelPos${i} F[LR]\n")
            fail("SplitPCM ${pcm_name}: HWChannelPos${i} missing or not FL/FR")
        endif()
    endforeach()
endforeach()

# ---------------------------------------------------------------------------
# Devices: expected table "<name>|<direction>|<ch0>|<ch1>|<comment>".
set(expected
    "Headphones 1|Playback|8|9|Headphones Output 9/10"
    "Headphones 2|Playback|10|11|Headphones Output 11/12"
    "Line 1|Playback|0|1|Analog Output 1/2"
    "Line 2|Playback|2|3|Analog Output 3/4"
    "Line 3|Playback|4|5|Analog Output 5/6"
    "Line 4|Playback|6|7|Analog Output 7/8"
    "Line 5|Capture|0|1|Analog Input 1/2")
set(DEFAULT_PLAYBACK "Headphones 1")

set(seen_names "")
set(used_Playback "")
set(used_Capture "")
set(best_prio -1)
set(best_dev "")
set(second_prio -1)

foreach(part IN LISTS parts)
    if(NOT part MATCHES "^SectionDevice\\.\"([^\"]+)\"")
        fail("cannot parse device section: ${part}")
        continue()
    endif()
    set(dev "${CMAKE_MATCH_1}")
    list(APPEND seen_names "${dev}")

    set(exp "")
    foreach(e IN LISTS expected)
        if(e MATCHES "^${dev}\\|")
            set(exp "${e}")
        endif()
    endforeach()
    if(exp STREQUAL "")
        fail("unexpected device '${dev}'")
        continue()
    endif()
    string(REPLACE "|" ";" exp "${exp}")
    list(GET exp 1 e_dir)
    list(GET exp 2 e_ch0)
    list(GET exp 3 e_ch1)
    list(GET exp 4 e_comment)

    if(NOT part MATCHES "Comment \"([^\"]*)\"")
        fail("${dev}: no Comment")
    elseif(NOT CMAKE_MATCH_1 STREQUAL e_comment)
        fail("${dev}: Comment '${CMAKE_MATCH_1}', expected '${e_comment}'")
    endif()

    if(e_dir STREQUAL "Playback")
        set(e_pcm ff802_stereo_out)
        set(other_dir Capture)
    else()
        set(e_pcm ff802_stereo_in)
        set(other_dir Playback)
    endif()

    string(REGEX MATCHALL "Macro\\.pcm_split\\.SplitPCMDevice" macros "${part}")
    list(LENGTH macros n_macros)
    if(NOT n_macros EQUAL 1)
        fail("${dev}: expected exactly one SplitPCMDevice, found ${n_macros}")
    endif()

    foreach(re "Name \"${e_pcm}\"" "Direction ${e_dir}" "HWChannels 22"
               "Channels 2\n" "Channel0 ${e_ch0}\n" "Channel1 ${e_ch1}\n"
               "ChannelPos0 FL" "ChannelPos1 FR")
        if(NOT part MATCHES "${re}")
            fail("${dev}: expected '${re}'")
        endif()
    endforeach()
    if(part MATCHES "Channel[2-9]|Direction ${other_dir}|${other_dir}Priority")
        fail("${dev}: unexpected extra channel or wrong direction")
    endif()
    foreach(ch ${e_ch0} ${e_ch1})
        if(ch GREATER_EQUAL 22)
            fail("${dev}: channel ${ch} out of range")
        endif()
        list(FIND used_${e_dir} "${ch}" idx)
        if(NOT idx EQUAL -1)
            fail("${dev}: ${e_dir} channel ${ch} used twice")
        endif()
        list(APPEND used_${e_dir} "${ch}")
    endforeach()

    if(NOT part MATCHES "${e_dir}Priority ([0-9]+)")
        fail("${dev}: ${e_dir}Priority missing")
    elseif(e_dir STREQUAL "Playback")
        set(p "${CMAKE_MATCH_1}")
        if(p GREATER best_prio)
            set(second_prio ${best_prio})
            set(best_prio ${p})
            set(best_dev "${dev}")
        elseif(p GREATER second_prio)
            set(second_prio ${p})
        endif()
    endif()

    # Digital interfaces must not be exposed (checked on name and comment;
    # the word "Direction" in the macro arguments is fine).
    if("${dev} ${e_comment}" MATCHES "SPDIF|S/PDIF|ADAT|AES|Direct|HDMI|Digital")
        fail("${dev}: digital interfaces must not be exposed")
    endif()
endforeach()

foreach(e IN LISTS expected)
    string(REGEX MATCH "^[^|]+" name "${e}")
    list(FIND seen_names "${name}" idx)
    if(idx EQUAL -1)
        fail("expected device '${name}' missing")
    endif()
endforeach()
list(LENGTH seen_names n_seen)
list(LENGTH expected n_expected)
if(NOT n_seen EQUAL n_expected)
    fail("expected ${n_expected} devices, found ${n_seen}")
endif()

if(NOT best_dev STREQUAL DEFAULT_PLAYBACK)
    fail("'${DEFAULT_PLAYBACK}' must have the highest PlaybackPriority (found '${best_dev}')")
elseif(NOT best_prio GREATER second_prio)
    fail("'${DEFAULT_PLAYBACK}' priority must be strictly highest")
endif()

if(errors GREATER 0)
    message(FATAL_ERROR "${errors} UCM profile check(s) failed")
endif()
message(STATUS "UCM profile OK: ${n_seen} devices, default playback '${best_dev}'")
