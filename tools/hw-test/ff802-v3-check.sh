#!/bin/sh
# ff802-v3-check.sh: read-only checks for the integration test of the
# mixer initialisation ff802-mixer-init-v3 (docs/mixer_v3_integration.md).
#
#   tools/hw-test/ff802-v3-check.sh pre            before the installation
#   tools/hw-test/ff802-v3-check.sh post <since>   after the power cycle,
#                                                  <since> as for journalctl
#                                                  (e.g. "10 min ago")
#
# Reads only: package files, dpkg status, the journal (journalctl -t
# ff802-cc-auto -o cat), /run/ff802-cc-auto, /var/lib/ff802-cc-auto and
# sysfs. Sends nothing, installs nothing, needs no root.
# Test overrides: FF802_CHECK_JOURNAL (file instead of journalctl),
# FF802_CHECK_ROOT (prefix for /run, /var/lib, /sys).

set -u
LC_ALL=C
export LC_ALL

NEW=build-deb-dev6/ff802-cc-auto_0.2.0~dev6_amd64.deb
NEW_SHA=0ad3512495b54cef07f952f79b47be3104bed1787ee09eedbd4093e2a7a791c5
DEV4=build-deb-dev4/ff802-cc-auto_0.2.0~dev4_amd64.deb
DEV4_SHA=f983b5df0aed25a5376c5a4e50dc9bcf0e51f5ff5e619d245c87cd39306574fb
ROOT=${FF802_CHECK_ROOT:-}
RUN=$ROOT/run/ff802-cc-auto
STATE=$ROOT/var/lib/ff802-cc-auto
fails=0

ok() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
info() { echo "info $*"; }
sha() { sha256sum "$1" 2>/dev/null | cut -d" " -f1; }

case ${1:-} in
pre)
    ok "dev6 package present, SHA-256 $NEW_SHA" '[ "$(sha "$NEW")" = "$NEW_SHA" ]'
    ok "dev4 package (way back) present and unchanged, SHA-256 $DEV4_SHA" '[ "$(sha "$DEV4")" = "$DEV4_SHA" ]'
    info "installed: $(dpkg-query -W -f='${Package} ${Version} ${Status}' ff802-cc-auto 2>/dev/null || echo none)"
    info "automation: $(cat "$STATE/mode" 2>/dev/null || echo off), mixer init: $(cat "$STATE/mixer-init" 2>/dev/null || echo off)"
    ;;
post)
    since=${2:?usage: ff802-v3-check.sh post <since>}
    J=$(mktemp)
    if [ -n "${FF802_CHECK_JOURNAL:-}" ]; then
        cp "$FF802_CHECK_JOURNAL" "$J"
    else
        journalctl -t ff802-cc-auto -o cat --no-pager --since "$since" > "$J" 2>/dev/null
    fi
    has() { grep -q -- "$1" "$J"; }
    cnt() { n=$(grep -c -- "$1" "$J"); echo "${n:-0}"; }
    info "installed: $(dpkg-query -W -f='${Version}' ff802-cc-auto 2>/dev/null || echo none); journal lines: $(wc -l < "$J")"
    if has "^CC command sent"; then
        ok "switch: CC command sent once" '[ "$(cnt "^CC command sent: 01 00 44 BD")" = 1 ]'
        ok "switch: port reset triggered (disable = 1, 2000 ms, = 0)" 'has "^Port reset triggered: .*disable = 1, 2000 ms, = 0"'
        ok "switch: re-enumerated in class-compliant mode (0424:3fdd)" 'has "^Re-enumerated in class-compliant mode: .* 0424:3fdd"'
        ok "switch: result switched, no failed reset" \
            'has "^Result: switched" && ! has "^Port reset failed" && ! has "^Result: \(reset-failed\|not-reenumerated\|reenumerated-not-cc\)"'
    else
        info "no CC command in the journal: the unit started in class-compliant mode (no switch needed)"
    fi
    ok "mixer: configuration v3 verified (38 entries, 0xEC858A44)" \
        'has "^mixer: configuration ff802-mixer-init-v3, 38 entries, checksum 0xEC858A44 (verified)\.$"'
    ok "mixer: entries 1..26 are v2 (0xC97A359C)" \
        'has "^mixer: entries 1\.\.26 are ff802-mixer-init-v2, checksum 0xC97A359C (verified)\.$"'
    ok "mixer: 38 registers set in the last run, the last one Input 9 -> Phones 12" \
        '[ "$(cnt "^mixer: set 0x.*\[[0-9]*/38\]$")" -ge 38 ] && has "^mixer: set 0x42C8 = 0x2666 (Input 9 -> Phones 12, 30 %) \[38/38\]$"'
    ok "mixer: the twelve Input 9 registers at 0x2666" \
        '[ "$(grep "^mixer: set 0x4[0-2][0-9A-F]8 = 0x2666 (Input 9 -> " "$J" | sed "s/ \[.*//" | sort -u | wc -l)" = 12 ]'
    ok "mixer: pass 2 sent: the same 38 values again (Playback -> Phones included)" \
        'has "^mixer: pass 1 of 2 complete; pass 2 (the same 38 values) in 5000 ms\.$" &&
         has "^mixer: pass 2: set 0x4228 = 0x9000 (Playback 9 -> Phones 9, 0 dB) \[11/38\]$" &&
         has "^mixer: pass 2: set 0x42C8 = 0x2666 (Input 9 -> Phones 12, 30 %) \[38/38\]$"'
    ok "mixer: result done (38 of 38), no FAILED line" \
        'has "^mixer: result done (38 of 38 registers): .*, twice$" && ! has "^mixer: \(pass 2: \)\{0,1\}FAILED"'
    m() { sed -n "s/^$1=//p" "$RUN/mixer-result" 2>/dev/null; }
    ok "mixer-result: done, 38/38, v3 0xEC858A44, base v2 0xC97A359C" \
        '[ "$(m outcome)" = done ] && [ "$(m written)" = 38/38 ] && [ "$(m config)" = ff802-mixer-init-v3 ] &&
         [ "$(m checksum)" = 0xEC858A44 ] && [ "$(m base)" = ff802-mixer-init-v2 ] && [ "$(m base_checksum)" = 0xC97A359C ]'
    port=$(m port)
    ok "unit now in class-compliant mode at $port (0424:3fdd)" \
        '[ -n "$port" ] && [ "$(cat "$ROOT/sys/bus/usb/devices/$port/idVendor" 2>/dev/null):$(cat "$ROOT/sys/bus/usb/devices/$port/idProduct" 2>/dev/null)" = 0424:3fdd ]'
    ok "no mixer-failed notification pending" '! grep -q "^kind=mixer-failed" "$RUN/mixer-event" 2>/dev/null || [ "$(sed -n "s/^devnum=//p" "$RUN/mixer-event")" != "$(m devnum)" ]'
    rm -f "$J"
    ;;
*)
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
[ "$fails" -eq 0 ] && echo "all checks passed" || echo "$fails check(s) FAILED"
[ "$fails" -eq 0 ]
