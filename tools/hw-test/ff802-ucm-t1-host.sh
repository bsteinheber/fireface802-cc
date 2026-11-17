#!/bin/bash
# ff802-ucm-t1-host.sh - complete host run of test T1 with guaranteed rollback.
#
# Run on the Kubuntu host as normal user (not root):
#
#   tools/hw-test/ff802-ucm-t1-host.sh
#
# Steps: record the state of every location T1 could conceivably affect,
# 'ff802-ucm-t1.sh prepare' and 'run', copy the results, then ALWAYS
# 'ff802-ucm-t1.sh clean' (EXIT trap, also on errors and Ctrl+C) and compare
# the state before and after.
#
# Output (git-ignored): build-t1/log.txt, env.txt, state-*.txt, results/
#
# Exit status:
#   0  T1 passed and rollback verified
#   1  T1 checks failed (rollback verified)
#   2  prepare failed or usage/environment error (rollback verified)
#   3  no Fireface 802 in CC mode found (rollback verified)
#   4  rollback NOT verified: clean failed or the state differs - check now
#   5  another run of this wrapper is active (nothing was done)
#   130 interrupted (Ctrl+C or signal), rollback verified
#
# Writes nothing except below the output directory and the private T1
# working directory of ff802-ucm-t1.sh. Concurrent runs are refused with a
# lock (flock) on this script file itself, so no lock file is created.
#
# Overrides for automated tests: FF802_T1_OUT (output directory) and the
# FF802_T1_* variables of ff802-ucm-t1.sh, which are passed through.
set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=$(cd -- "$SCRIPT_DIR/../.." && pwd)
T1=$SCRIPT_DIR/ff802-ucm-t1.sh
OUT=${FF802_T1_OUT:-$REPO/build-t1}
WORK=${FF802_T1_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/ff802-ucm-t1}

if [ "$(id -u)" = 0 ]; then
    echo "ff802-ucm-t1-host: do not run as root" >&2
    exit 2
fi
# Two runs would share the T1 working directory, and one run's 'clean'
# would remove the other's overlay. Lock before anything is touched.
if command -v flock >/dev/null 2>&1; then
    exec 9< "$0"
    if ! flock -n 9; then
        echo "ff802-ucm-t1-host: another run is active; nothing was done" >&2
        exit 5
    fi
else
    echo "ff802-ucm-t1-host: flock not available; concurrent runs are not prevented" >&2
fi
mkdir -p -- "$OUT" || exit 2
rm -rf -- "$OUT/results" "$OUT/state-before.txt" "$OUT/state-after.txt" \
          "$OUT/log.txt" "$OUT/env.txt"
exec > >(tee "$OUT/log.txt") 2>&1

# Read-only snapshot of every location T1 could conceivably affect.
snapshot() {
    {
        for p in /usr/share/alsa /etc/alsa /etc/asound.conf "$HOME/.asoundrc" \
                 "$HOME/.config/alsa" /etc/pipewire "$HOME/.config/pipewire" \
                 /etc/wireplumber "$HOME/.config/wireplumber" \
                 /etc/udev/rules.d "$HOME/.config/environment.d"; do
            if [ -e "$p" ]; then
                find "$p" -xdev \( -type f -o -type l \) -print0 2>/dev/null | sort -z |
                    xargs -0 -r sha256sum 2>/dev/null
            else
                echo "ABSENT $p"
            fi
        done
        echo "--- T1 working directory"
        if [ -e "$WORK" ]; then echo "PRESENT $WORK"; else echo "ABSENT $WORK"; fi
        echo "--- user session environment (ALSA/PIPEWIRE/SPA/WIREPLUMBER)"
        systemctl --user show-environment 2>/dev/null |
            grep -E '^(ALSA|PIPEWIRE|SPA|WIREPLUMBER)' || echo "(none)"
    } > "$1"
}

rc_prep=skipped
rc_run=skipped
rollback_ok=0
interrupted=0

finish() {
    trap - EXIT INT TERM HUP
    echo "=== clean (always executed)"
    "$T1" clean
    rc_clean=$?
    echo "clean exit: $rc_clean"
    snapshot "$OUT/state-after.txt"
    echo "=== state diff before/after (empty = identical)"
    if diff "$OUT/state-before.txt" "$OUT/state-after.txt"; then
        echo "IDENTICAL"
        [ "$rc_clean" = 0 ] && [ ! -e "$WORK" ] && rollback_ok=1
    fi
    echo "=== wireplumber state dir (may change on its own, informational)"
    ls -la --time-style=full-iso "$HOME/.local/state/wireplumber" 2>&1
    echo "=== services after"
    systemctl --user is-active pipewire wireplumber 2>&1

    if [ "$rollback_ok" != 1 ]; then
        status=4
    elif [ "$interrupted" = 1 ]; then
        status=130
    elif [ "$rc_prep" != 0 ]; then
        status=2
    else
        case $rc_run in
            0) status=0 ;;
            1|3) status=$rc_run ;;
            *) status=2 ;;
        esac
    fi
    echo "=== summary: prepare=$rc_prep run=$rc_run clean=$rc_clean rollback=$([ "$rollback_ok" = 1 ] && echo verified || echo NOT-VERIFIED) exit=$status"
    case $status in
        0) echo "T1 PASSED" ;;
        4) echo "ROLLBACK NOT VERIFIED - inspect $OUT/state-*.txt and $WORK" ;;
        130) echo "T1 INTERRUPTED (no result)" ;;
        *) echo "T1 FAILED" ;;
    esac
    exit "$status"
}

echo "=== environment"
{
    date -u '+%Y-%m-%dT%H:%M:%SZ'; uname -r; grep PRETTY_NAME /etc/os-release
    echo "user: $(id -un) uid $(id -u)"
    echo "repo HEAD: $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null) dirty: $(git -C "$REPO" status --short 2>/dev/null | wc -l)"
    dpkg-query -W -f='${Package} ${Version}\n' alsa-ucm-conf libasound2t64 alsa-utils pipewire wireplumber 2>&1
    command -v "${FF802_T1_ALSAUCM:-alsaucm}"
    echo "ALSA_CONFIG_UCM2 in shell: ${ALSA_CONFIG_UCM2:-<unset>}"
    echo "--- lsusb"; lsusb 2>&1 | grep -iE '0424|2a39|RME|Fireface'
    echo "--- cards"; cat "${FF802_T1_PROC_ASOUND:-/proc/asound}/cards"
    echo "--- usbid per card"
    for d in "${FF802_T1_PROC_ASOUND:-/proc/asound}"/card[0-9]*; do
        echo "${d##*/}: $(cat "$d/usbid" 2>/dev/null || echo '-') $(cat "$d/id" 2>/dev/null)"
    done
    sys=${FF802_T1_SYSTEM_UCM2:-/usr/share/alsa/ucm2}
    echo "--- profile targets in system tree (must be absent)"
    for f in USB-Audio/conf.d/0424-3fdd.conf USB-Audio/RME/Fireface-802.conf USB-Audio/RME/Fireface-802-HiFi.conf; do
        ls -l "$sys/$f" 2>&1
    done
    echo "--- conf.d hook"; grep -n 'conf.d' "$sys/USB-Audio/USB-Audio.conf"
    echo "--- services before"; systemctl --user is-active pipewire wireplumber 2>&1
    echo "--- wpctl status (read-only)"
    command -v wpctl >/dev/null && wpctl status 2>&1 | sed -n '1,60p'
} 2>&1 | tee "$OUT/env.txt"

snapshot "$OUT/state-before.txt"
trap finish EXIT
trap 'interrupted=1; echo "interrupted"; exit 130' INT TERM HUP

echo "=== prepare"
rc_prep=interrupted
"$T1" prepare
rc_prep=$?
echo "prepare exit: $rc_prep"
if [ "$rc_prep" = 0 ]; then
    echo "=== run"
    rc_run=interrupted
    "$T1" run
    rc_run=$?
    echo "run exit: $rc_run"
    if [ -d "$WORK/results" ]; then
        cp -a "$WORK/results" "$OUT/results"
        cp -a "$WORK/manifest.txt" "$OUT/results/" 2>/dev/null
    fi
fi
