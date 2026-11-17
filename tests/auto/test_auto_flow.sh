#!/bin/sh
# T4 service-level test: what systemd would run for udev events, with the
# test build ff802-cc-autoswitch-sim (simulated USB, no libusb) below a
# private root. The command line is taken from ff802-cc-auto@.service
# (ExecStart, %i) and the instance from the udev rule
# (%k:$env{BUSNUM}:$env{DEVNUM}), so the tested invocation is the
# installed one with only the program path replaced.
#
# Usage: test_auto_flow.sh <source dir> <sim program> <real program> <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
SIM=$2
REAL=$3
T=$4
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
    */flow-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
R=$T/root
SVC=$SRC/packaging/ff802-cc-auto/ff802-cc-auto@.service
RULE=$SRC/packaging/ff802-cc-auto/70-ff802-cc-auto.rules
PORT=3-2.4.1.1.3
DEVS=$R/sys/bus/usb/devices
unset FF802_SIM_INIT_ERR FF802_SIM_LAYOUT FF802_SIM_OPEN_ERR FF802_SIM_KDRV \
      FF802_SIM_CLAIM_ERR FF802_SIM_WRITE_ERR FF802_SIM_WRITE_SHORT \
      FF802_SIM_REPLUG_ON_OPEN FF802_SIM_UNPLUG_ON_OPEN FF802_SIM_UNPLUG_ON_WRITE \
      FF802_SIM_WRITE_DELAY_MS INVOCATION_ID

# --- simulated host -------------------------------------------------------------
reset_root() {
    rm -rf -- "${R:?}"
    mkdir -p "$DEVS" "$R/state" "$R/run"
}
dev() { # dev <name> <vid> <pid> <bus> <devnum> [<product>] [<manufacturer>]
    d=$DEVS/$1
    mkdir -p "$d"
    echo "$2" > "$d/idVendor"; echo "$3" > "$d/idProduct"
    echo "$4" > "$d/busnum"; echo "$5" > "$d/devnum"; echo 480 > "$d/speed"
    echo "${7:-RME}" > "$d/manufacturer"
    echo "${6:-Fireface 802 (24047128)}" > "$d/product"
}
prop() { dev "$PORT" 2a39 3fcd 3 "$1"; }
cc() { dev "$PORT" 0424 3fdd 3 "$1"; }
unplug() { rm -rf -- "${DEVS:?}/$PORT"; }
mode() { echo "$1" > "$R/state/mode"; }

# udev: the instance for an "add" event, from the rule's SYSTEMD_WANTS
WANTS=$(sed -n 's/^ACTION=="add", ENV{SYSTEMD_WANTS}+="\(.*\)"$/\1/p' "$RULE")
instance() { # instance <kernel name>: like udev, BUSNUM/DEVNUM zero-padded
    b=$(printf '%03d' "$(cat "$DEVS/$1/busnum")")
    n=$(printf '%03d' "$(cat "$DEVS/$1/devnum")")
    unit=$(printf '%s\n' "$WANTS" | sed -e "s/%k/$1/" -e "s/\$env{BUSNUM}/$b/" -e "s/\$env{DEVNUM}/$n/")
    unit=${unit#ff802-cc-auto@}
    printf '%s\n' "${unit%.service}"
}
# systemd: ExecStart of the unit with %i, the program replaced by SIM
EXEC=$(sed -n 's/^ExecStart=//p' "$SVC")
service() { # service <instance> [env...]: run like systemd, print the exit code
    inst=$1; shift
    cmd=$(printf '%s\n' "$EXEC" | sed -e "s|^/usr/libexec/ff802-cc/ff802-cc-autoswitch|$SIM|" -e "s/%i/$inst/")
    # shellcheck disable=SC2086
    env INVOCATION_ID=flowtest FF802_AUTO_ROOT="$R" "$@" $cmd >> "$T/journal.txt" 2>&1
    echo $?
}
event() { service "$(instance "$PORT")" "$@"; }   # udev "add" for the Fireface
writes() { n=$(grep -c '^write ' "$R/usb.log" 2>/dev/null); echo "${n:-0}"; }
usb_calls() { [ -f "$R/usb.log" ] && wc -l < "$R/usb.log" || echo 0; }
kind() { sed -n 's/^kind=//p' "$R/run/event" 2>/dev/null; }
outcome() { sed -n 's/^outcome=//p' "$R/run/last-result" 2>/dev/null; }

ok "udev rule and unit give the instance <kernel>:<busnum>:<devnum>" \
    'reset_root; prop 19; [ "$(instance $PORT)" = "3-2.4.1.1.3:003:019" ] &&
     [ "$EXEC" = "/usr/libexec/ff802-cc/ff802-cc-autoswitch --service %i" ]'

# --- 1 / 10: CC mode ---------------------------------------------------------------
reset_root; mode on; cc 19
rc=$(event)
ok "1: already in CC mode: exit 0, USB not even initialised, no notification" \
    '[ "$rc" = 0 ] && [ "$(usb_calls)" = 0 ] && [ ! -f "$R/run/event" ] && [ "$(outcome)" = cc-active ]'

# --- 2 / 3 / 10: proprietary -> enable once -> reconnect in CC ----------------------
reset_root; mode on; prop 18
rc=$(event)
ok "2/3: proprietary mode: one transfer 01 00 44 BD to 0x0C of bus 3 device 18, event 'prepared'" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] &&
     grep -qx "write ep=0x0C bytes=01 00 44 BD bus=3 addr=18 timeout=1000" "$R/usb.log" &&
     [ "$(kind)" = prepared ] && [ "$(outcome)" = sent ] && [ -f "$R/run/pending" ]'
ok "2/3: claim and release around the single write, device closed, backend released" \
    '[ "$(grep -v "^list\|^layout" "$R/usb.log" | tr "\n" ";")" = "init;open 3:18;kdrv if 1;claim if 1;write ep=0x0C bytes=01 00 44 BD bus=3 addr=18 timeout=1000;release if 1;close;exit;" ]'
for i in 1 2 3; do rc=$(event); done
ok "6: repeated events for the same connection: no further USB access" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] && [ "$(outcome)" = already-handled ] &&
     [ "$(grep -c "^init" "$R/usb.log")" = 1 ] && [ "$(sed -n "s/^seq=//p" "$R/run/event")" = 1 ]'
unplug; cc 19
rc=$(event)
ok "10: reconnected in CC mode: confirmation event once, pending cleared, no USB access" \
    '[ "$rc" = 0 ] && [ "$(kind)" = cc-active ] && [ ! -f "$R/run/pending" ] && [ "$(writes)" = 1 ] &&
     [ "$(grep -c "^init" "$R/usb.log")" = 1 ]'
rc=$(event)
ok "10: further CC events: no second confirmation" \
    '[ "$(sed -n "s/^seq=//p" "$R/run/event")" = 2 ]'

# --- 4: failed transfer --------------------------------------------------------------
for err in timeout pipe io; do
    reset_root; mode on; prop 18
    rc=$(event FF802_SIM_WRITE_ERR=$err)
    rc2=$(event)
    ok "4: transfer fails ($err): exit 4, event 'failed', no retry on the next event" \
        '[ "$rc" = 4 ] && [ "$(writes)" = 1 ] && [ "$(kind)" = failed ] && [ "$rc2" = 0 ] &&
         [ "$(outcome)" = already-handled ] && [ ! -f "$R/run/pending" ]'
done
reset_root; mode on; prop 18
rc=$(event FF802_SIM_WRITE_SHORT=2)
ok "4: incomplete transfer (2 of 4 bytes): exit 4, state unknown" \
    '[ "$rc" = 4 ] && [ "$(writes)" = 1 ] && grep -q "^detail=transfer failed (incomplete" "$R/run/event"'
for f in FF802_SIM_INIT_ERR=1 FF802_SIM_OPEN_ERR=access FF802_SIM_KDRV=1 FF802_SIM_LAYOUT=bad FF802_SIM_CLAIM_ERR=busy; do
    reset_root; mode on; prop 18
    rc=$(event $f)
    ok "4: failure before the write ($f): exit 3, nothing written, event 'failed'" \
        '[ "$rc" = 3 ] && [ "$(writes)" = 0 ] && [ "$(kind)" = failed ] && grep -q "nothing sent" "$R/run/event"'
done

# --- 5: device removed during the run -------------------------------------------
reset_root; mode on; prop 18
inst=$(instance $PORT); unplug
rc=$(service "$inst")
ok "5: removed before the service runs: exit 3, no USB access, no notification" \
    '[ "$rc" = 3 ] && [ "$(usb_calls)" = 0 ] && [ ! -f "$R/run/event" ] && [ "$(outcome)" = refused ]'
for f in FF802_SIM_UNPLUG_ON_OPEN=1 FF802_SIM_REPLUG_ON_OPEN=1; do
    reset_root; mode on; prop 18
    rc=$(event $f)
    ok "5: removed/reconnected between detection and write ($f): not written, not claimed" \
        '[ "$rc" = 3 ] && [ "$(writes)" = 0 ] && ! grep -q "^claim" "$R/usb.log" && grep -q "^close" "$R/usb.log"'
done
reset_root; mode on; prop 18
rc=$(event FF802_SIM_UNPLUG_ON_WRITE=1)
ok "5: removed during the write: exit 4, reported as disconnected, no retry" \
    '[ "$rc" = 4 ] && [ "$(writes)" = 1 ] && grep -q "device was disconnected" "$R/run/event"'
reset_root; mode on; prop 18
inst=$(instance $PORT); prop 19
rc=$(service "$inst")
ok "5: reconnected before the service runs (stale event devnum 18, now 19): nothing for the stale event" \
    '[ "$rc" = 3 ] && [ "$(usb_calls)" = 0 ] && grep -q "reconnected since the event" "$R/run/last-result"'

# --- 7 / 8: identity ---------------------------------------------------------------
reset_root; mode on
dev 1-4 0424 2514 1 3 "USB2514B" "Microchip"
rc=$(service "$(instance 1-4)")
ok "7: unknown USB device: exit 3, no USB access, no notification" \
    '[ "$rc" = 3 ] && [ "$(usb_calls)" = 0 ] && [ ! -f "$R/run/event" ]'
dev 1-5 2a39 3fcd 1 4 "Fireface 802 FS (12345678)"
rc=$(service "$(instance 1-5)")
dev 1-6 2a39 3fcd 1 5 "Fireface 802 (1)" "Microchip"
rc2=$(service "$(instance 1-6)")
ok "8: right USB ID but not identifiable as an original Fireface 802: refused, no USB access" \
    '[ "$rc" = 3 ] && [ "$rc2" = 3 ] && [ "$(usb_calls)" = 0 ] && [ "$(kind)" = failed ]'
rc=$(service "1-4:002:003")
ok "8: instance not matching the device's bus: usage error, nothing read" \
    '[ "$rc" = 2 ]'

# --- 11 / 12: automation off ----------------------------------------------------------
reset_root; prop 18
rc=$(event)
ok "11: automation off by default (no state file): exit 0, no USB access" \
    '[ "$rc" = 0 ] && [ "$(usb_calls)" = 0 ] && [ "$(outcome)" = off ] && [ ! -f "$R/run/event" ]'
mode off
rc=$(event)
rm -rf -- "${R:?}/run"; mkdir -p "$R/run"       # reboot: /run is empty
rc2=$(event)
ok "11/12: automation off, also after a reboot: no USB access" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(usb_calls)" = 0 ] && [ "$(outcome)" = off ]'

# --- 6 / 18: simultaneous events ---------------------------------------------------
reset_root; mode on; prop 18
inst=$(instance $PORT)
pids=""
for i in 1 2 3 4 5 6 7 8 9 10; do
    (service "$inst" FF802_SIM_WRITE_DELAY_MS=200 > "$T/rc.$i") &
    pids="$pids $!"
done
for p in $pids; do wait "$p"; done
ok "18: ten simultaneous events for one connection: exactly one transfer, all exit 0" \
    '[ "$(writes)" = 1 ] && [ "$(cat "$T"/rc.* | sort -u)" = 0 ] &&
     [ "$(grep -c "^Result: already-handled" "$T/journal.txt")" -ge 9 ]'
reset_root; mode on; prop 18
old=$(instance $PORT); prop 19; new=$(instance $PORT)
(service "$old" FF802_SIM_WRITE_DELAY_MS=200 > "$T/rc.old") & p1=$!
(service "$new" FF802_SIM_WRITE_DELAY_MS=200 > "$T/rc.new") & p2=$!
wait $p1; wait $p2
ok "18: stale and current event at the same time: only the current connection is written" \
    '[ "$(writes)" = 1 ] && grep -q "bus=3 addr=19" "$R/usb.log" && [ "$(cat "$T/rc.old")" = 3 ] &&
     [ "$(cat "$T/rc.new")" = 0 ]'

# --- guards against unintended use ------------------------------------------------
reset_root; mode on; prop 18
FF802_AUTO_ROOT=$R "$SIM" --service "$(instance $PORT)" > "$T/manual.txt" 2>&1
rc=$?
ok "manual start without systemd (no INVOCATION_ID): refused, nothing read or written" \
    '[ "$rc" = 2 ] && [ "$(usb_calls)" = 0 ] && [ ! -e "$R/run/lock" ] &&
     grep -q "only started by ff802-cc-auto@.service" "$T/manual.txt"'
for a in "" "--service" "--service 3-2:3:18 extra" "--enable" "3-2:3:18"; do
    # shellcheck disable=SC2086
    env INVOCATION_ID=x FF802_AUTO_ROOT="$R" "$SIM" $a > /dev/null 2>&1
    rc=$?
    ok "bad arguments '$a': usage error, nothing done" '[ "$rc" = 2 ] && [ "$(usb_calls)" = 0 ]'
done
env INVOCATION_ID=x "$SIM" --service "$(instance $PORT)" > /dev/null 2>&1
rc=$?
ok "simulation refuses to run without its private root (never the real /run, /sys)" '[ "$rc" = 2 ]'

before=$(cd "$R" && find . | sort)
FF802_AUTO_ROOT=$R "$SIM" --status > "$T/status.txt" 2>&1
rc=$?
ok "--status is read only and reports identity and the automation" \
    '[ "$rc" = 0 ] && [ "$(cd "$R" && find . | sort)" = "$before" ] && [ "$(usb_calls)" = 0 ] &&
     grep -q "^automation: on" "$T/status.txt" &&
     grep -q "identity: Fireface 802, proprietary mode" "$T/status.txt" &&
     grep -q "would send the CC enable command" "$T/status.txt"'

# the installed program: same guards, checked before anything is opened
if [ "$(id -u)" != 0 ]; then
    "$REAL" --service 3-2.4.1.1.3:003:018 > "$T/real1.txt" 2>&1; rc1=$?
    env INVOCATION_ID=x "$REAL" --service 3-2.4.1.1.3:003:018 > "$T/real2.txt" 2>&1; rc2=$?
    ok "installed program: refused without systemd and as a normal user (before any USB or /run access)" \
        '[ "$rc1" = 2 ] && [ "$rc2" = 2 ] && grep -q "runs as the system service only" "$T/real2.txt"'
else
    echo "skip installed-program guard test (running as root)"
fi
"$REAL" --version > "$T/realv.txt" 2>&1
ok "installed program of this build: --version reports the write gate, no simulation" \
    'grep -Eq "^ff802-cc-autoswitch [0-9.]+ \(USB writes (enabled|disabled)\)$" "$T/realv.txt"'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
