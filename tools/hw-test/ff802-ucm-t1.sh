#!/bin/sh
# ff802-ucm-t1.sh - isolated, read-only host test T1 for the Fireface 802
# ALSA UCM profile.
#
#   prepare   build a private copy of the system UCM tree plus the profile
#             files from this repository (below ~/.cache/ff802-ucm-t1)
#   run       evaluate the profile with alsaucm against the real card,
#             using ALSA_CONFIG_UCM2 for these alsaucm processes only
#   clean     remove the private copy again (this is the complete rollback)
#
# What this script never does: write to /usr/share/alsa, /etc or any
# PipeWire/WirePlumber configuration, open a PCM, play or record audio,
# set a UCM verb or device, run boot/default sequences, restart services,
# or need root. alsaucm only opens the card's control device for reading.
#
# Overrides for automated tests (not needed on the host):
#   FF802_T1_DIR          working directory (default ~/.cache/ff802-ucm-t1)
#   FF802_T1_SYSTEM_UCM2  system UCM tree   (default /usr/share/alsa/ucm2)
#   FF802_T1_PROC_ASOUND  procfs ALSA dir   (default /proc/asound)
#   FF802_T1_ALSAUCM      alsaucm binary    (default alsaucm)
#   FF802_T1_REPO_UCM2    profile files     (default <repo>/audio/ucm2)

set -eu

PROG=ff802-ucm-t1
USBID="0424:3fdd"
MARKER=".ff802-ucm-t1"

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK_DIR=${FF802_T1_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/ff802-ucm-t1}
SYSTEM_UCM2=${FF802_T1_SYSTEM_UCM2:-/usr/share/alsa/ucm2}
PROC_ASOUND=${FF802_T1_PROC_ASOUND:-/proc/asound}
ALSAUCM=${FF802_T1_ALSAUCM:-alsaucm}
REPO_UCM2=${FF802_T1_REPO_UCM2:-$SCRIPT_DIR/../../audio/ucm2}

OVERLAY=$WORK_DIR/ucm2
RESULTS=$WORK_DIR/results

# Profile files of this project, relative to the UCM root.
PROFILE_FILES="USB-Audio/conf.d/0424-3fdd.conf
USB-Audio/RME/Fireface-802.conf
USB-Audio/RME/Fireface-802-HiFi.conf"

# Devices the HiFi verb must provide.
EXPECTED_DEVICES="Headphones 1
Headphones 2
Line 1
Line 2
Line 3
Line 4
Line 5"

die() { echo "$PROG: error: $*" >&2; exit 2; }
info() { echo "$PROG: $*"; }

usage() {
    cat <<EOF
Usage: $PROG prepare | run | clean | --help

Isolated, read-only test T1 of the Fireface 802 UCM profile.
Working directory: $WORK_DIR
EOF
}

refuse_root() {
    if [ "$(id -u)" = "0" ]; then
        die "do not run this script as root"
    fi
}

# Remove the working directory, but only if it was created by this script.
remove_work_dir() {
    if [ -e "$WORK_DIR" ]; then
        if [ ! -f "$WORK_DIR/$MARKER" ]; then
            die "$WORK_DIR exists but was not created by $PROG; not touching it"
        fi
        rm -rf -- "${WORK_DIR:?}"
    fi
}

# Abort if files on the evaluation path contain directives that would
# write ALSA controls or execute commands.
safety_scan() {
    # Loops read from here-documents so that die() ends the script itself
    # (a while loop in a pipeline would run in a subshell).
    while IFS= read -r rel; do
        f=$OVERLAY/$rel
        [ -f "$f" ] || die "missing file in overlay: $f"
        if grep -Eq '(^|[^A-Za-z])(cset|cset-new|cset-tlv|exec|shell|sysw|BootSequence|FixedBootSequence|SectionDefaults)([^A-Za-z]|$)' "$f"; then
            die "unexpected control/exec directive in $f"
        fi
    done <<EOF
USB-Audio/USB-Audio.conf
common/pcm/split.conf
$PROFILE_FILES
EOF
}

cmd_prepare() {
    refuse_root
    [ -f "$SYSTEM_UCM2/USB-Audio/USB-Audio.conf" ] ||
        die "system UCM tree not found: $SYSTEM_UCM2"
    while IFS= read -r rel; do
        [ -f "$REPO_UCM2/$rel" ] || die "profile file missing in repository: $rel"
        [ ! -e "$SYSTEM_UCM2/$rel" ] ||
            die "system already provides $rel; T1 would shadow it - aborting"
    done <<EOF
$PROFILE_FILES
EOF
    # shellcheck disable=SC2016 # the ${var:...} text is UCM syntax
    grep -Fq '/USB-Audio/conf.d/${var:USBID1}-${var:USBID2}.conf' \
        "$SYSTEM_UCM2/USB-Audio/USB-Audio.conf" ||
        die "system USB-Audio.conf has no conf.d hook for per-device files"

    remove_work_dir
    mkdir -p "$WORK_DIR"
    : > "$WORK_DIR/$MARKER"
    mkdir -p "$OVERLAY" "$RESULTS"
    cp -RP "$SYSTEM_UCM2/." "$OVERLAY/"

    while IFS= read -r rel; do
        mkdir -p "$(dirname -- "$OVERLAY/$rel")"
        cp "$REPO_UCM2/$rel" "$OVERLAY/$rel"
    done <<EOF
$PROFILE_FILES
EOF

    safety_scan

    {
        echo "created: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        echo "system ucm2: $SYSTEM_UCM2"
        if command -v dpkg-query >/dev/null 2>&1; then
            # shellcheck disable=SC2016 # dpkg-query format, not a variable
            echo "alsa-ucm-conf: $(dpkg-query -W -f='${Version}' alsa-ucm-conf 2>/dev/null || echo unknown)"
        fi
        while IFS= read -r rel; do
            echo "profile: $rel $(cksum < "$OVERLAY/$rel" | cut -d' ' -f1)"
        done <<EOF
$PROFILE_FILES
EOF
    } > "$WORK_DIR/manifest.txt"

    info "overlay prepared in $OVERLAY"
    info "nothing outside $WORK_DIR was changed"
}

# Print the index of the single ALSA card with USB id 0424:3fdd.
# Returns 1 if there is none and 2 if there are several. It runs inside a
# command substitution, so it must not call die() itself.
find_card() {
    found=""
    for d in "$PROC_ASOUND"/card[0-9]*; do
        [ -f "$d/usbid" ] || continue
        if [ "$(cat "$d/usbid")" = "$USBID" ]; then
            [ -z "$found" ] || return 2
            found=${d##*/card}
        fi
    done
    [ -n "$found" ] || return 1
    echo "$found"
}

# Run alsaucm with the private UCM tree; output goes to a result file.
ucm() {
    out=$1
    shift
    status=0
    ALSA_CONFIG_UCM2=$OVERLAY "$ALSAUCM" "$@" > "$RESULTS/$out" 2>&1 || status=$?
    echo "exit status: $status" >> "$RESULTS/$out"
    return 0
}

check() {
    if eval "$2"; then
        echo "  PASS  $1"
    else
        echo "  FAIL  $1"
        failed=$((failed + 1))
    fi
}

cmd_run() {
    refuse_root
    [ -f "$WORK_DIR/$MARKER" ] && [ -d "$OVERLAY" ] ||
        die "overlay not prepared; run '$PROG prepare' first"
    command -v "$ALSAUCM" >/dev/null 2>&1 || die "alsaucm not found"
    safety_scan

    card_rc=0
    card=$(find_card) || card_rc=$?
    case $card_rc in
        0) ;;
        1) echo "$PROG: no ALSA card with USB id $USBID (is the Fireface in CC mode?)" >&2
           exit 3 ;;
        *) die "more than one ALSA card with USB id $USBID; refusing to guess" ;;
    esac
    longname=$(sed -n "s/^ *$card \[[^]]*\]: *//p; /^ *$card \[/{n;s/^ *//p;}" "$PROC_ASOUND/cards" 2>/dev/null | tail -n 1)
    info "card hw:$card: $longname"
    printf 'card: hw:%s\nlong name: %s\n' "$card" "$longname" > "$RESULTS/card.txt"

    # Baseline with the unmodified system tree (expected to be unsupported).
    status=0
    "$ALSAUCM" -c "hw:$card" list _verbs > "$RESULTS/baseline-verbs.txt" 2>&1 || status=$?
    echo "exit status: $status" >> "$RESULTS/baseline-verbs.txt"

    ucm verbs.txt         -c "hw:$card" list _verbs
    ucm devices.txt       -c "hw:$card" list _devices/HiFi
    ucm dump.txt          -c "hw:$card" dump text
    ucm dump-split.txt    -c "<<<SplitPCM=1>>>hw:$card" dump text

    failed=0
    echo "T1 results (details in $RESULTS):"
    check "card long name names the original Fireface 802" \
        'printf "%s" "$longname" | grep -q "Fireface 802" && ! printf "%s" "$longname" | grep -q "802 FS"'
    check "HiFi verb is offered" 'grep -q "HiFi" "$RESULTS/verbs.txt"'
    while IFS= read -r dev; do
        check "device '$dev' is listed" 'grep -Fq "$dev" "$RESULTS/devices.txt"'
    done <<EOF
$EXPECTED_DEVICES
EOF
    check "dump succeeds" 'grep -q "exit status: 0" "$RESULTS/dump.txt"'
    check "dump without SplitPCM refers to the ff802 split PCMs" \
        'grep -q "ff802_stereo_out" "$RESULTS/dump.txt" && grep -q "ff802_stereo_in" "$RESULTS/dump.txt"'
    check "SplitPCM dump succeeds" 'grep -q "exit status: 0" "$RESULTS/dump-split.txt"'
    check "SplitPCM dump reports 22 hardware playback channels" \
        'grep -Eq "PlaybackChannels[^0-9]*22" "$RESULTS/dump-split.txt"'
    check "SplitPCM dump reports 22 hardware capture channels" \
        'grep -Eq "CaptureChannels[^0-9]*22" "$RESULTS/dump-split.txt"'
    check "SplitPCM dump uses the hw PCM of the card" \
        'grep -Eq "PlaybackPCM.*hw:" "$RESULTS/dump-split.txt"'

    info "no PCM was opened, no audio was played, no configuration was changed"
    if [ "$failed" -ne 0 ]; then
        info "$failed check(s) failed - inspect $RESULTS"
        exit 1
    fi
    info "all checks passed"
}

cmd_clean() {
    refuse_root
    remove_work_dir
    info "removed $WORK_DIR (complete rollback of T1)"
}

case ${1:-} in
    prepare) cmd_prepare ;;
    run)     cmd_run ;;
    clean)   cmd_clean ;;
    -h|--help) usage ;;
    *) usage >&2; exit 2 ;;
esac
