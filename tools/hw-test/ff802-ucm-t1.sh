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
LOADER_ID="ff802-t1"

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

# Devices the HiFi verb must provide, as alsa-lib reports them.
# alsa-lib >= 1.2.15 normalizes device names ("Line 1" becomes "Line1",
# see verb_normalize_device_names() in src/ucm/parser.c).
# Fields: name|direction|channel pair|comment|priority
EXPECTED_DEVICES="Headphones1|Playback|8 9|Headphones Output 9/10|255
Headphones2|Playback|10 11|Headphones Output 11/12|200
Line1|Playback|0 1|Analog Output 1/2|192
Line2|Playback|2 3|Analog Output 3/4|191
Line3|Playback|4 5|Analog Output 5/6|190
Line4|Playback|6 7|Analog Output 7/8|189
Line5|Capture|0 1|Analog Input 1/2|192"

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
        [ -n "$rel" ] || continue
        f=$OVERLAY/$rel
        [ -f "$f" ] || die "missing file in overlay: $f"
        if grep -Eq '(^|[^A-Za-z])(cset|cset-new|cset-tlv|exec|shell|sysw|BootSequence|FixedBootSequence|SectionDefaults)([^A-Za-z]|$)' "$f"; then
            die "unexpected control/exec directive in $f"
        fi
    done <<EOF
USB-Audio/USB-Audio.conf
common/pcm/split.conf
$PROFILE_FILES
$(cat "$WORK_DIR/scan-extra" 2>/dev/null || true)
EOF
}

# Make the private copy of USB-Audio.conf load our conf.d file.
#
# The system file includes USB-Audio/conf.d/<vid>-<pid>.conf with
# "Optional true". alsa-lib before 1.2.16 checks the existence of an
# optional include with access() on the unresolved path
# ("/USB-Audio/conf.d/0424-3fdd.conf", relative to the UCM root, not to the
# file system), so the file is never loaded (fixed upstream in alsa-lib
# commit 62c24074 "ucm: fix optional include"). In the private copy only,
# a block is inserted before "If.inc" that includes the file without
# "Optional", guarded by the USB id. With a fixed alsa-lib the file is
# simply evaluated twice, which defines the same value twice.
insert_loader() {
    f=$OVERLAY/USB-Audio/USB-Audio.conf
    n=$(grep -c '^If\.inc[[:space:]]*{' "$f" || true)
    [ "$n" = 1 ] || die "expected exactly one 'If.inc {' line in $f, found $n"
    ! grep -q "If\.$LOADER_ID" "$f" || die "$f already contains If.$LOADER_ID"
    line=$(grep -n '^If\.inc[[:space:]]*{' "$f" | cut -d: -f1)
    {
        head -n "$((line - 1))" "$f"
        # shellcheck disable=SC2016 # ${CardComponents} is UCM syntax
        printf '%s\n' \
            "# Inserted by $PROG into its private copy only (alsa-lib < 1.2.16" \
            "# skips optional includes, see tools/hw-test/ff802-ucm-t1.sh)." \
            "If.$LOADER_ID {" \
            '	Condition {' \
            '		Type String' \
            '		Haystack "${CardComponents}"' \
            '		Needle "USB0424:3fdd"' \
            '	}' \
            '	True.Include.ff802.File "/USB-Audio/conf.d/0424-3fdd.conf"' \
            '}' \
            ''
        tail -n "+$line" "$f"
    } > "$f.ff802-new"
    rm -f -- "$f"
    mv -- "$f.ff802-new" "$f"
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
    insert_loader
    echo "overlay change: If.$LOADER_ID inserted into USB-Audio/USB-Audio.conf" > "$WORK_DIR/layout"
    safety_scan

    {
        echo "created: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        echo "system ucm2: $SYSTEM_UCM2"
        if command -v dpkg-query >/dev/null 2>&1; then
            # shellcheck disable=SC2016 # dpkg-query format, not a variable
            echo "alsa-ucm-conf: $(dpkg-query -W -f='${Version}' alsa-ucm-conf 2>/dev/null || echo unknown)"
            # shellcheck disable=SC2016
            echo "alsa-lib: $(dpkg-query -W -f='${Version}' libasound2t64 2>/dev/null || echo unknown)"
        fi
        cat "$WORK_DIR/layout"
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

# Run alsaucm with the given UCM tree; output goes to a result file.
ucm_with() {
    tree=$1
    out=$2
    shift 2
    status=0
    ALSA_CONFIG_UCM2=$tree "$ALSAUCM" "$@" > "$RESULTS/$out" 2>&1 || status=$?
    echo "exit status: $status" >> "$RESULTS/$out"
    return 0
}

# Print the value of <key> of device <dev> from an alsaucm text dump,
# without the quotes added by alsaucm.
dump_value() {
    awk -v dev="$2" -v key="$3" '
        /^\tDevice\./ {
            d = $0
            sub(/^\tDevice\./, "", d); sub(/ \{$/, "", d); gsub(/"/, "", d)
            cur = d
            next
        }
        /^\t\}/ { cur = "" }
        cur == dev && $1 == key {
            v = $0
            sub(/^[\t ]*[^\t ]+ /, "", v); gsub(/"/, "", v)
            print v
            exit
        }' "$1"
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
    # /proc/asound/cards: " N [id    ]: driver - short name" followed by a
    # line with the long name.
    longname=$(sed -n "/^ *$card \[/{n;s/^ *//p;q;}" "$PROC_ASOUND/cards" 2>/dev/null)
    info "card hw:$card: $longname"
    printf 'card: hw:%s\nlong name: %s\n' "$card" "$longname" > "$RESULTS/card.txt"

    # Baseline with the unmodified system tree (expected to be unsupported).
    ucm_with "$SYSTEM_UCM2" baseline-verbs.txt -c "hw:$card" list _verbs

    ucm_with "$OVERLAY" verbs.txt         -c "hw:$card" list _verbs
    ucm_with "$OVERLAY" devices.txt       -c "hw:$card" list _devices/HiFi
    ucm_with "$OVERLAY" dump.txt          -c "hw:$card" dump text
    ucm_with "$OVERLAY" dump-split.txt    -c "<<<SplitPCM=1>>>hw:$card" dump text

    R=$RESULTS
    failed=0
    echo "T1 results (details in $RESULTS):"
    check "card long name names the original Fireface 802" \
        'printf "%s" "$longname" | grep -q "Fireface 802" && ! printf "%s" "$longname" | grep -q "802 FS"'
    check "baseline: system tree alone has no UCM profile for the card" \
        'grep -q "UCM is not supported" "$R/baseline-verbs.txt"'
    for f in verbs devices dump dump-split; do
        check "$f: alsaucm succeeds without ALSA errors or warnings" \
            'grep -qx "exit status: 0" "$R/$f.txt" && ! grep -Eqi "ALSA lib|error|warning" "$R/$f.txt"'
    done
    check "HiFi is the only verb" \
        '[ "$(grep -Ec "^  [0-9]+: " "$R/verbs.txt")" = 1 ] && grep -qx "  0: HiFi" "$R/verbs.txt"'
    check "exactly 7 devices" '[ "$(grep -Ec "^  [0-9]+: " "$R/devices.txt")" = 7 ]'

    while IFS='|' read -r dev dir pair comment prio; do
        c0=${pair% *}
        c1=${pair#* }
        if [ "$dir" = Playback ]; then split=ff802_stereo_out; else split=ff802_stereo_in; fi
        check "$dev: listed with comment '$comment'" \
            'grep -A1 -Ex "  [0-9]+: $dev" "$R/devices.txt" | grep -qx "    $comment"'
        check "$dev: ${dir}PCM is $split channels $c0,$c1, 2 channels, priority $prio" \
            'dump_value "$R/dump.txt" "$dev" ${dir}PCM | grep -Eq "(^|\.)$split:[^,]+,0,$c0,$c1\$" &&
             [ "$(dump_value "$R/dump.txt" "$dev" ${dir}Channels)" = 2 ] &&
             [ "$(dump_value "$R/dump.txt" "$dev" ${dir}Priority)" = "$prio" ]'
        check "$dev: SplitPCM gives hw PCM, 22 channels, channels $c0/$c1 as FL/FR" \
            'dump_value "$R/dump-split.txt" "$dev" ${dir}PCM | grep -Eq "(^|\.)hw:[^,]+,0\$" &&
             [ "$(dump_value "$R/dump-split.txt" "$dev" ${dir}Channels)" = 22 ] &&
             [ "$(dump_value "$R/dump-split.txt" "$dev" ${dir}Channel0)" = "$c0" ] &&
             [ "$(dump_value "$R/dump-split.txt" "$dev" ${dir}Channel1)" = "$c1" ] &&
             [ "$(dump_value "$R/dump-split.txt" "$dev" ${dir}ChannelPos0)" = FL ] &&
             [ "$(dump_value "$R/dump-split.txt" "$dev" ${dir}ChannelPos1)" = FR ]'
    done <<EOF
$EXPECTED_DEVICES
EOF

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
