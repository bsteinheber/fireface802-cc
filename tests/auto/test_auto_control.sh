#!/bin/sh
# T4: turning the automation on and off (ff802-cc-auto, its root helpers,
# polkit policy) and the effect on the service (simulation build).
#
# Usage: test_auto_control.sh <source dir> <sim program> <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
SIM=$2
T=$3
P=$SRC/packaging/ff802-cc-auto
fails=0
checks=0

ok() {
    checks=$((checks + 1))
    if eval "$2"; then
        echo "ok   $1"
    else
        echo "FAIL $1"
        fails=$((fails + 1))
    fi
}

case $T in
    */control-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
R=$T/root
PORT=3-2.4.1.1.3
DEVS=$R/sys/bus/usb/devices
STATE=$R/var/lib/ff802-cc-auto
RUN=$R/run/ff802-cc-auto

# helpers as installed, the simulation build as ff802-cc-autoswitch
LX=$T/libexec
mkdir -p "$LX"
ln -s "$SIM" "$LX/ff802-cc-autoswitch"
for f in ff802-cc-auto-on ff802-cc-auto-off ff802-cc-auto-mixer-on ff802-cc-auto-mixer-off ff802-cc-notify; do
    ln -s "$P/$f" "$LX/$f"
done
CTL="sh $P/ff802-cc-auto"
ctl() { FF802_AUTO_ROOT=$R FF802_AUTO_LIBEXEC=$LX $CTL "$@"; }

reset() {
    rm -rf -- "${R:?}"
    mkdir -p "$DEVS/$PORT" "$RUN"
    d=$DEVS/$PORT
    echo 2a39 > "$d/idVendor"; echo 3fcd > "$d/idProduct"; echo 3 > "$d/busnum"
    echo 18 > "$d/devnum"; echo 480 > "$d/speed"; echo RME > "$d/manufacturer"
    echo "Fireface 802 (24047128)" > "$d/product"
}
event() {
    n=$(printf '%03d' "$(cat "$DEVS/$PORT/devnum")")
    env INVOCATION_ID=t FF802_AUTO_ROOT="$R" "$SIM" --service "$PORT:003:$n" >> "$T/journal.txt" 2>&1
    echo $?
}
writes() { n=$(grep -c '^write ' "$R/usb.log" 2>/dev/null); echo "${n:-0}"; }

# --- status, default -----------------------------------------------------------------
reset
ctl status > "$T/status0.txt" 2>&1; rc=$?
ok "status without any state: automation off, device shown, nothing written" \
    '[ "$rc" = 0 ] && grep -q "^automation: off" "$T/status0.txt" &&
     grep -q "identity: Fireface 802, proprietary mode" "$T/status0.txt" &&
     grep -q "off, nothing will be sent" "$T/status0.txt" && [ ! -e "$STATE" ] && [ "$(writes)" = 0 ]'
ok "status lists the installed components and where the log is" \
    'grep -q "MISSING  /usr/lib/udev/rules.d/70-ff802-cc-auto.rules" "$T/status0.txt" &&
     grep -q "present  $LX/ff802-cc-autoswitch" "$T/status0.txt" &&
     grep -q "journalctl -t ff802-cc-auto -b" "$T/status0.txt"'

# --- on / off ---------------------------------------------------------------------------
ctl on > "$T/on.txt" 2>&1; rc=$?
ok "on: mode file 'on' (0644 in a 0755 directory), hint about reconnecting" \
    '[ "$rc" = 0 ] && [ "$(cat "$STATE/mode")" = on ] && [ "$(stat -c %a "$STATE/mode")" = 644 ] &&
     [ "$(stat -c %a "$STATE")" = 755 ] && [ ! -e "$STATE/mode.tmp" ] && grep -q "disconnect and reconnect" "$T/on.txt"'
ok "on: no USB access by turning it on (applies from the next connection)" '[ "$(writes)" = 0 ] && [ ! -e "$R/usb.log" ]'
ctl status > "$T/status1.txt" 2>&1
ok "status: automation on, would switch on the next event" \
    'grep -q "^automation: on" "$T/status1.txt" && grep -q "would send the CC enable command" "$T/status1.txt"'
rc=$(event)
ok "on: the next connection event switches once" '[ "$rc" = 0 ] && [ "$(writes)" = 1 ]'

reset; ctl off > "$T/off.txt" 2>&1; rc=$?
ok "off: mode file 'off', explains that the USB mode of the device is not changed" \
    '[ "$rc" = 0 ] && [ "$(cat "$STATE/mode")" = off ] && grep -q "USB mode of the device is not changed" "$T/off.txt"'
rc=$(event)
ok "11: automation off: connection event sends nothing" '[ "$rc" = 0 ] && [ "$(writes)" = 0 ]'
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"; echo 19 > "$DEVS/$PORT/devnum"
rc=$(event)
ok "12: still off after a reboot (/run empty) and a new connection" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 0 ] && [ "$(cat "$STATE/mode")" = off ]'

# 13/14: on -> device switched back to proprietary outside Linux (TotalMix
# FX on Windows or ARC), user wants to stay there: off, then reconnect
reset; ctl on > /dev/null 2>&1
rc=$(event)
ctl off > /dev/null 2>&1
echo 20 > "$DEVS/$PORT/devnum"; rc2=$(event)
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"; echo 21 > "$DEVS/$PORT/devnum"; rc3=$(event)
ok "14: after turning the automation off, a proprietary device stays proprietary (reconnect, reboot)" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] && [ "$rc2" = 0 ] && [ "$rc3" = 0 ]'

# --- mixer-init on / off / status ------------------------------------------------------
cc_unit() { # the unit in CC mode with its sound card and sequencer port
    d=$DEVS/$PORT
    echo 0424 > "$d/idVendor"; echo 3fdd > "$d/idProduct"; echo "$1" > "$d/devnum"
    mkdir -p "$d/$PORT:1.0/sound/card1/midiC1D0"
    printf '20 0 kernel 1 w Fireface 802 (24047128) Port 1\n20 1 kernel 1 w Fireface 802 (24047128) Port 2\n' > "$R/seq-ports"
}
cc_event() {
    n=$(printf '%03d' "$(cat "$DEVS/$PORT/devnum")")
    env INVOCATION_ID=t FF802_AUTO_ROOT="$R" "$SIM" --service "$PORT:003:$n" >> "$T/journal.txt" 2>&1
    echo $?
}
sysex() { n=$(grep -c '^sysex F0' "$R/midi.log" 2>/dev/null); echo "${n:-0}"; }

reset; cc_unit 19
ctl mixer-init status > "$T/ms0.txt" 2>&1; rc=$?
ok "mixer-init status without any state: off, nothing written" \
    '[ "$rc" = 0 ] && grep -q "^mixer init: off" "$T/ms0.txt" && grep -q "is not changed" "$T/ms0.txt" &&
     [ ! -e "$STATE" ] && [ ! -e "$R/midi.log" ]'
rc=$(cc_event)
ok "mixer-init off by default: a CC connection does not touch the mixer" '[ "$rc" = 0 ] && [ "$(sysex)" = 0 ] && [ ! -e "$R/midi.log" ]'

ctl mixer-init on > "$T/mon.txt" 2>&1; rc=$?
ok "mixer-init on: warning first, mixer-init 'on' (0644), automation mode untouched, nothing sent now" \
    '[ "$rc" = 0 ] && [ "$(cat "$STATE/mixer-init")" = on ] && [ "$(stat -c %a "$STATE/mixer-init")" = 644 ] &&
     [ ! -e "$STATE/mode" ] && [ ! -e "$STATE/mixer-init.tmp" ] && [ ! -e "$R/midi.log" ] &&
     [ "$(sed -n 1p "$T/mon.txt")" = "WARNING: with the mixer initialisation on, every connection of the" ] &&
     grep -q "headphone outputs can be very loud" "$T/mon.txt" && grep -q "nothing was sent now" "$T/mon.txt"'
ctl mixer-init status > "$T/ms1.txt" 2>&1
ctl status > "$T/s1.txt" 2>&1
ok "mixer-init status and status report it as on" \
    'grep -q "^mixer init: on" "$T/ms1.txt" && grep -q "are set to 0 dB" "$T/ms1.txt" &&
     grep -q "^mixer init: on" "$T/s1.txt" && grep -q "^automation: off" "$T/s1.txt"'
echo 20 > "$DEVS/$PORT/devnum"; rc=$(cc_event)
ok "mixer-init on: the next CC connection sets the 24 registers (automation off)" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 24 ] && [ "$(writes)" = 0 ]'
ctl mixer-init status > "$T/ms2.txt" 2>&1
ok "mixer-init status shows the last initialisation" \
    'grep -q "^last mixer initialisation:" "$T/ms2.txt" && grep -q "outcome=done" "$T/ms2.txt"'

ctl mixer-init off > "$T/moff.txt" 2>&1; rc=$?
echo 21 > "$DEVS/$PORT/devnum"; rc2=$(cc_event)
ok "mixer-init off: 'off', explains that the current settings stay; next connection: mixer untouched" \
    '[ "$rc" = 0 ] && [ "$(cat "$STATE/mixer-init")" = off ] && grep -q "current mixer settings of the unit are not changed" "$T/moff.txt" &&
     [ "$rc2" = 0 ] && [ "$(sysex)" = 24 ]'

for a in "mixer-init" "mixer-init yes" "mixer-init on extra" "mixer on" "status mixer-init"; do
    # shellcheck disable=SC2086
    ctl $a > /dev/null 2>&1; rc=$?
    ok "ff802-cc-auto '$a': usage error, mixer-init unchanged" '[ "$rc" = 2 ] && [ "$(cat "$STATE/mixer-init")" = off ]'
done
printf '#!/bin/sh\necho "mixer-init-stub $*" >> "%s"\nexit 0\n' "$T/mi.log" > "$T/mi-stub"
chmod +x "$T/mi-stub"
FF802_MIXER_INIT=$T/mi-stub ctl mixer-init dry-run > "$T/mdry.txt" 2>&1; rc=$?
FF802_MIXER_INIT=$T/mi-stub ctl mixer-init apply > "$T/mapply.txt" 2>&1; rc2=$?
ok "mixer-init dry-run/apply: run ff802-mixer-init by hand (apply after the warning), state unchanged" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] &&
     [ "$(tr "\n" ";" < "$T/mi.log")" = "mixer-init-stub --dry-run;mixer-init-stub --apply;" ] &&
     ! grep -q WARNING "$T/mdry.txt" && grep -q "headphone outputs can be very loud" "$T/mapply.txt" &&
     [ "$(cat "$STATE/mixer-init")" = off ]'
FF802_AUTO_ROOT=$R sh "$P/ff802-cc-auto-mixer-on" extra > /dev/null 2>&1; rc=$?
ok "mixer helper with arguments: refused" '[ "$rc" = 2 ] && [ "$(cat "$STATE/mixer-init")" = off ]'

# --- invalid states and arguments ------------------------------------------------
reset; mkdir -p "$STATE"; echo yes > "$STATE/mode"
rc=$(event)
ctl status > "$T/status2.txt" 2>&1
ok "unknown content of the mode file counts as off" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 0 ] && grep -q "^automation: invalid (treated as off)" "$T/status2.txt"'
for a in "" "enable" "disable" "on off" "--force"; do
    # shellcheck disable=SC2086
    ctl $a > /dev/null 2>&1; rc=$?
    ok "ff802-cc-auto '$a': usage error, mode unchanged" '[ "$rc" = 2 ] && [ "$(cat "$STATE/mode")" = yes ]'
done
FF802_AUTO_ROOT=$R sh "$P/ff802-cc-auto-on" extra > /dev/null 2>&1; rc=$?
ok "helper with arguments: refused" '[ "$rc" = 2 ] && [ "$(cat "$STATE/mode")" = yes ]'

# --- privileges: normal user without the test root ---------------------------------
if [ "$(id -u)" != 0 ]; then
    env -u FF802_AUTO_ROOT sh "$P/ff802-cc-auto-on" > "$T/h.txt" 2>&1; rc=$?
    ok "helper as a normal user (no pkexec/sudo): refused, nothing written" \
        '[ "$rc" = 2 ] && grep -q "needs root" "$T/h.txt"'
    mkdir -p "$T/nopk"
    for tool in sh sed id; do ln -sf "$(command -v $tool)" "$T/nopk/$tool"; done
    env -i PATH="$T/nopk" FF802_AUTO_LIBEXEC=$LX sh "$P/ff802-cc-auto" on > "$T/np.txt" 2>&1; rc=$?
    ok "on as a normal user without pkexec: refused with a hint for an administrator" \
        '[ "$rc" = 2 ] && grep -q "sudo ff802-cc-auto on" "$T/np.txt"'
    mkdir -p "$T/pk"
    for tool in sh sed id; do ln -sf "$(command -v $tool)" "$T/pk/$tool"; done
    printf '#!/bin/sh\necho "pkexec $*" >> "%s"\nexit 0\n' "$T/pkexec.log" > "$T/pk/pkexec"
    chmod +x "$T/pk/pkexec"
    for m in on off "mixer-init on" "mixer-init off"; do
        # shellcheck disable=SC2086
        env -i PATH="$T/pk" FF802_AUTO_LIBEXEC=/usr/libexec/ff802-cc sh "$P/ff802-cc-auto" $m > /dev/null 2>&1
    done
    ok "on/off/mixer-init on/off as a normal user: pkexec with exactly the helper of the polkit action" \
        '[ "$(cat "$T/pkexec.log" | tr "\n" ";")" = "pkexec /usr/libexec/ff802-cc/ff802-cc-auto-on;pkexec /usr/libexec/ff802-cc/ff802-cc-auto-off;pkexec /usr/libexec/ff802-cc/ff802-cc-auto-mixer-on;pkexec /usr/libexec/ff802-cc/ff802-cc-auto-mixer-off;" ]'
    env -i PATH="$T/nopk" FF802_AUTO_LIBEXEC=$LX sh "$P/ff802-cc-auto" mixer-init on > "$T/npm.txt" 2>&1; rc=$?
    ok "mixer-init on as a normal user without pkexec: refused with a hint for an administrator" \
        '[ "$rc" = 2 ] && grep -q "sudo ff802-cc-auto mixer-init on" "$T/npm.txt"'
    env -u FF802_AUTO_ROOT sh "$P/ff802-cc-auto-mixer-on" > "$T/hm.txt" 2>&1; rc=$?
    ok "mixer helper as a normal user (no pkexec/sudo): refused, nothing written" \
        '[ "$rc" = 2 ] && grep -q "needs root" "$T/hm.txt"'
else
    echo "skip privilege tests (running as root)"
fi

# --- polkit policy -----------------------------------------------------------------
POL=$P/org.ff802cc.auto.policy
action() { awk -v id="$1" '/<action id="/ { on = index($0, "\"" id "\"") > 0 } on' "$POL"; }
ok "polkit: 'on' needs administrator authentication, bound to the on helper" \
    'action org.ff802cc.auto.on | grep -q "<allow_active>auth_admin_keep</allow_active>" &&
     action org.ff802cc.auto.on | grep -q "<allow_any>auth_admin</allow_any>" &&
     action org.ff802cc.auto.on | grep -q "exec.path\">/usr/libexec/ff802-cc/ff802-cc-auto-on<"'
ok "polkit: 'off' allowed for the active local user, bound to the off helper" \
    'action org.ff802cc.auto.off | grep -q "<allow_active>yes</allow_active>" &&
     action org.ff802cc.auto.off | grep -q "<allow_inactive>auth_admin</allow_inactive>" &&
     action org.ff802cc.auto.off | grep -q "exec.path\">/usr/libexec/ff802-cc/ff802-cc-auto-off<"'
ok "polkit: exactly four actions, no other permissions (no 'yes' for on, nothing for any user)" \
    '[ "$(grep -c "<action id=" "$POL")" = 4 ] && [ "$(grep -c "<allow_active>yes" "$POL")" = 2 ] &&
     ! grep -q "<allow_any>yes" "$POL" && ! grep -q "<allow_inactive>yes" "$POL"'
ok "polkit: 'mixer-on' needs administrator authentication, bound to the mixer-on helper" \
    'action org.ff802cc.auto.mixer-on | grep -q "<allow_active>auth_admin_keep</allow_active>" &&
     action org.ff802cc.auto.mixer-on | grep -q "<allow_any>auth_admin</allow_any>" &&
     action org.ff802cc.auto.mixer-on | grep -q "exec.path\">/usr/libexec/ff802-cc/ff802-cc-auto-mixer-on<"'
ok "polkit: 'mixer-off' allowed for the active local user, bound to the mixer-off helper" \
    'action org.ff802cc.auto.mixer-off | grep -q "<allow_active>yes</allow_active>" &&
     action org.ff802cc.auto.mixer-off | grep -q "<allow_inactive>auth_admin</allow_inactive>" &&
     action org.ff802cc.auto.mixer-off | grep -q "exec.path\">/usr/libexec/ff802-cc/ff802-cc-auto-mixer-off<"'
balanced() { for e in policyconfig action defaults; do
    [ "$(grep -c "<$e[ >]" "$POL")" = "$(grep -c "</$e>" "$POL")" ] || return 1
done; }
ok "polkit: well-formed (balanced elements)" balanced

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
