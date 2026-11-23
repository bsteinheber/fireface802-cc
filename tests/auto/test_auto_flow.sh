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
RUN=$R/run/ff802-cc-auto
STATE=$R/var/lib/ff802-cc-auto
unset FF802_SIM_INIT_ERR FF802_SIM_LAYOUT FF802_SIM_OPEN_ERR FF802_SIM_KDRV \
      FF802_SIM_CLAIM_ERR FF802_SIM_WRITE_ERR FF802_SIM_WRITE_SHORT \
      FF802_SIM_REPLUG_ON_OPEN FF802_SIM_UNPLUG_ON_OPEN FF802_SIM_UNPLUG_ON_WRITE \
      FF802_SIM_WRITE_DELAY_MS INVOCATION_ID \
      FF802_SIM_SEQ_OPEN_ERR FF802_SIM_SEQ_LIST_ERR FF802_SIM_SEQ_CONNECT_ERR \
      FF802_SIM_SEQ_SEND_ERR_AT FF802_SIM_MIXER_WRITE FF802_SIM_MIXER_TIMEOUT_MS \
      FF802_SIM_SEQ_PORTS_AFTER

# --- simulated host -------------------------------------------------------------
reset_root() {
    rm -rf -- "${R:?}"
    mkdir -p "$DEVS" "$STATE" "$RUN"
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
mode() { echo "$1" > "$STATE/mode"; }

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
kind() { sed -n 's/^kind=//p' "$RUN/event" 2>/dev/null; }
outcome() { sed -n 's/^outcome=//p' "$RUN/last-result" 2>/dev/null; }

ok "udev rule and unit give the instance <kernel>:<busnum>:<devnum>" \
    'reset_root; prop 19; [ "$(instance $PORT)" = "3-2.4.1.1.3:003:019" ] &&
     [ "$EXEC" = "/usr/libexec/ff802-cc/ff802-cc-autoswitch --service %i" ]'

# --- 1 / 10: CC mode ---------------------------------------------------------------
reset_root; mode on; cc 19
rc=$(event)
ok "1: already in CC mode: exit 0, USB not even initialised, no notification" \
    '[ "$rc" = 0 ] && [ "$(usb_calls)" = 0 ] && [ ! -f "$RUN/event" ] && [ "$(outcome)" = cc-active ]'

# --- 2 / 3 / 10: proprietary -> enable once -> reconnect in CC ----------------------
reset_root; mode on; prop 18
rc=$(event)
ok "2/3: proprietary mode: one transfer 01 00 44 BD to 0x0C of bus 3 device 18, event 'prepared'" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] &&
     grep -qx "write ep=0x0C bytes=01 00 44 BD bus=3 addr=18 timeout=1000" "$R/usb.log" &&
     [ "$(kind)" = prepared ] && [ "$(outcome)" = sent ] && [ -f "$RUN/pending" ]'
ok "2/3: claim and release around the single write, device closed, backend released" \
    '[ "$(grep -v "^list\|^layout" "$R/usb.log" | tr "\n" ";")" = "init;open 3:18;kdrv if 1;claim if 1;write ep=0x0C bytes=01 00 44 BD bus=3 addr=18 timeout=1000;release if 1;close;exit;" ]'
for i in 1 2 3; do rc=$(event); done
ok "6: repeated events for the same connection: no further USB access" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] && [ "$(outcome)" = already-handled ] &&
     [ "$(grep -c "^init" "$R/usb.log")" = 1 ] && [ "$(sed -n "s/^seq=//p" "$RUN/event")" = 1 ]'
unplug; cc 19
rc=$(event)
ok "10: reconnected in CC mode: confirmation event once, pending cleared, no USB access" \
    '[ "$rc" = 0 ] && [ "$(kind)" = cc-active ] && [ ! -f "$RUN/pending" ] && [ "$(writes)" = 1 ] &&
     [ "$(grep -c "^init" "$R/usb.log")" = 1 ]'
rc=$(event)
ok "10: further CC events: no second confirmation" \
    '[ "$(sed -n "s/^seq=//p" "$RUN/event")" = 2 ]'

# --- 4: failed transfer --------------------------------------------------------------
for err in timeout pipe io; do
    reset_root; mode on; prop 18
    rc=$(event FF802_SIM_WRITE_ERR=$err)
    rc2=$(event)
    ok "4: transfer fails ($err): exit 4, event 'failed', no retry on the next event" \
        '[ "$rc" = 4 ] && [ "$(writes)" = 1 ] && [ "$(kind)" = failed ] && [ "$rc2" = 0 ] &&
         [ "$(outcome)" = already-handled ] && [ ! -f "$RUN/pending" ]'
done
reset_root; mode on; prop 18
rc=$(event FF802_SIM_WRITE_SHORT=2)
ok "4: incomplete transfer (2 of 4 bytes): exit 4, state unknown" \
    '[ "$rc" = 4 ] && [ "$(writes)" = 1 ] && grep -q "^detail=transfer failed (incomplete" "$RUN/event"'
for f in FF802_SIM_INIT_ERR=1 FF802_SIM_OPEN_ERR=access FF802_SIM_KDRV=1 FF802_SIM_LAYOUT=bad FF802_SIM_CLAIM_ERR=busy; do
    reset_root; mode on; prop 18
    rc=$(event $f)
    ok "4: failure before the write ($f): exit 3, nothing written, event 'failed'" \
        '[ "$rc" = 3 ] && [ "$(writes)" = 0 ] && [ "$(kind)" = failed ] && grep -q "nothing sent" "$RUN/event"'
done

# --- 5: device removed during the run -------------------------------------------
reset_root; mode on; prop 18
inst=$(instance $PORT); unplug
rc=$(service "$inst")
ok "5: removed before the service runs: exit 3, no USB access, no notification" \
    '[ "$rc" = 3 ] && [ "$(usb_calls)" = 0 ] && [ ! -f "$RUN/event" ] && [ "$(outcome)" = refused ]'
for f in FF802_SIM_UNPLUG_ON_OPEN=1 FF802_SIM_REPLUG_ON_OPEN=1; do
    reset_root; mode on; prop 18
    rc=$(event $f)
    ok "5: removed/reconnected between detection and write ($f): not written, not claimed" \
        '[ "$rc" = 3 ] && [ "$(writes)" = 0 ] && ! grep -q "^claim" "$R/usb.log" && grep -q "^close" "$R/usb.log"'
done
reset_root; mode on; prop 18
rc=$(event FF802_SIM_UNPLUG_ON_WRITE=1)
ok "5: removed during the write: exit 4, reported as disconnected, no retry" \
    '[ "$rc" = 4 ] && [ "$(writes)" = 1 ] && grep -q "device was disconnected" "$RUN/event"'
reset_root; mode on; prop 18
inst=$(instance $PORT); prop 19
rc=$(service "$inst")
ok "5: reconnected before the service runs (stale event devnum 18, now 19): nothing for the stale event" \
    '[ "$rc" = 3 ] && [ "$(usb_calls)" = 0 ] && grep -q "reconnected since the event" "$RUN/last-result"'

# --- 7 / 8: identity ---------------------------------------------------------------
reset_root; mode on
dev 1-4 0424 2514 1 3 "USB2514B" "Microchip"
rc=$(service "$(instance 1-4)")
ok "7: unknown USB device: exit 3, no USB access, no notification" \
    '[ "$rc" = 3 ] && [ "$(usb_calls)" = 0 ] && [ ! -f "$RUN/event" ]'
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
    '[ "$rc" = 0 ] && [ "$(usb_calls)" = 0 ] && [ "$(outcome)" = off ] && [ ! -f "$RUN/event" ]'
mode off
rc=$(event)
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"       # reboot: /run is empty
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

# --- mixer initialisation in CC mode ------------------------------------------------
MIDI=$R/midi.log
mixer() { echo "$1" > "$STATE/mixer-init"; }
sndcard() { mkdir -p "$DEVS/$PORT/$PORT:1.0/sound/card1/midiC1D0"; }
seqports() { # the listing of the unit in phase 2a; client number as argument
    cat > "$R/seq-ports" <<EOF
0 0 kernel -1 - Timer
14 0 kernel -1 w Midi Through Port-0
${1:-20} 0 kernel 1 w Fireface 802 (24047128) Port 1
${1:-20} 1 kernel 1 w Fireface 802 (24047128) Port 2
144 0 user -1 w input
EOF
}
ccdev() { cc "$1"; sndcard; seqports "${2:-20}"; }
sysex() { n=$(grep -c '^sysex F0' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
midi_calls() { [ -f "$MIDI" ] && wc -l < "$MIDI" || echo 0; }
mres() { sed -n "s/^$1=//p" "$RUN/mixer-result" 2>/dev/null; }
mkind() { sed -n 's/^kind=//p' "$RUN/mixer-event" 2>/dev/null; }
nth_sysex() { grep '^sysex F0' "$MIDI" | sed -n "$1p"; }

reset_root; ccdev 19
rc=$(event)
ok "M1: mixer init off by default: CC event opens no MIDI, writes no mixer result" \
    '[ "$rc" = 0 ] && [ "$(midi_calls)" = 0 ] && [ ! -e "$RUN/mixer-result" ] &&
     [ ! -e "$RUN/mixer-event" ] && [ "$(outcome)" = cc-active ]'

reset_root; mode off; mixer on; ccdev 19
rc=$(event)
ok "M2: mixer init on, automation off: 26 SysEx messages to port 20:1, exit 0, no USB access" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 26 ] && [ "$(usb_calls)" = 0 ] &&
     grep -qx "seq connect 20:1" "$MIDI" && [ "$(mres outcome)" = done ] &&
     [ "$(mres written)" = 26/26 ] && [ "$(mres card)" = 1 ] &&
     [ "$(mres midi_port)" = "20:1 Fireface 802 (24047128) Port 2" ] && [ ! -e "$RUN/mixer-event" ]'
ok "M2: order: the two crossed routings off first, then routings 1..12, output volumes last" \
    '[ "$(nth_sysex 1)" = "sysex F0 00 20 0D 10 00 00 00 00 11 04 F7" ] &&
     [ "$(nth_sysex 2)" = "sysex F0 00 20 0D 10 00 00 00 04 13 04 F7" ] &&
     [ "$(nth_sysex 3)" = "sysex F0 00 20 0D 10 00 00 20 02 01 0C F7" ] &&
     [ "$(nth_sysex 4)" = "sysex F0 00 20 0D 10 00 00 20 06 03 0C F7" ] &&
     [ "$(nth_sysex 11)" = "sysex F0 00 20 0D 10 00 00 20 22 11 0C F7" ] &&
     [ "$(nth_sysex 23)" = "sysex F0 00 20 0D 10 00 00 00 00 30 02 F7" ] &&
     [ "$(tr "\n" ";" < "$MIDI" | sed "s/sysex F0[^;]*;//g")" = "seq open;seq list;seq connect 20:1;seq close;" ]'
ok "M2: one journal line per register, last-result stays cc-active" \
    '[ "$(grep -c "^mixer: set 0x" "$T/journal.txt")" = 26 ] &&
     grep -q "^mixer: set 0x4228 = 0x9000 (Playback 9 -> Phones 9, 0 dB) \[11/26\]$" "$T/journal.txt" &&
     grep -q "^mixer: set 0x2900 = 0x0000 (Output Phones 12 volume, 0 dB) \[26/26\]$" "$T/journal.txt" &&
     [ "$(outcome)" = cc-active ] && grep -q "mixer initialisation: done" "$RUN/last-result"'

rc=$(event)
ok "M3: a repeated add event of the same connection initialises again (same values), exit 0" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 52 ] && [ "$(nth_sysex 27)" = "$(nth_sysex 1)" ]'
unplug; ccdev 20 24
rc=$(event)
ok "M3: new connection, client number changed (24): found by card, initialised" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 78 ] && grep -qx "seq connect 24:1" "$MIDI"'
unplug; ccdev 19
rc=$(event)
ok "M4: reused device number (19 again): the initialisation is never suppressed" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 104 ] && [ "$(mres devnum)" = 19 ] && [ "$(mres outcome)" = done ]'

reset_root; mode on; mixer on; prop 18
rc=$(event)
ok "M5: proprietary mode: only the switch, no MIDI access" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] && [ "$(midi_calls)" = 0 ] && [ ! -e "$RUN/mixer-result" ]'
unplug; ccdev 19
rc=$(event)
ok "M5: after the switch, reconnected in CC mode: cc-active event and mixer done, kept apart" \
    '[ "$rc" = 0 ] && [ "$(kind)" = cc-active ] && [ "$(sysex)" = 26 ] && [ ! -e "$RUN/mixer-event" ] &&
     [ "$(mres outcome)" = done ]'

reset_root; mixer on; cc 19
rc=$(event)
ok "M6: no sound card within the timeout: exit 3, no-port, sequencer never opened, mixer-failed event" \
    '[ "$rc" = 3 ] && [ "$(midi_calls)" = 0 ] && [ "$(mres outcome)" = no-port ] &&
     [ "$(mkind)" = mixer-failed ] && grep -q "^detail=.*no sound card with MIDI device.*(0 of 26 registers)" "$RUN/mixer-event" &&
     [ ! -e "$RUN/event" ] && [ "$(outcome)" = cc-active ]'
sndcard
rc=$(event)
ok "M7: sound card but no sequencer port: exit 3, no-port, nothing sent" \
    '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ] && ! grep -q "^seq connect" "$MIDI" &&
     [ "$(mres outcome)" = no-port ] && grep -q "seq close" "$MIDI"'

for f in FF802_SIM_SEQ_OPEN_ERR=1 FF802_SIM_SEQ_LIST_ERR=1 FF802_SIM_SEQ_CONNECT_ERR=busy; do
    reset_root; mixer on; ccdev 19
    rc=$(event $f)
    ok "M8: $f: exit 3, not-sent, no SysEx, mixer-failed event" \
        '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ] && [ "$(mres outcome)" = not-sent ] && [ "$(mkind)" = mixer-failed ]'
done

reset_root; mixer on; ccdev 19
rc=$(event FF802_SIM_SEQ_SEND_ERR_AT=7)
ok "M9: sending fails at register 7: exit 4, incomplete, 6 sent, nothing retried" \
    '[ "$rc" = 4 ] && [ "$(sysex)" = 6 ] && [ "$(mres outcome)" = incomplete ] && [ "$(mres written)" = 6/26 ] &&
     [ "$(grep -c "^sysex FAILED" "$MIDI")" = 1 ] && grep -q "(6 of 26 registers)" "$RUN/mixer-event" &&
     grep -q "seq close" "$MIDI"'

reset_root; mixer on; ccdev 19
rc=$(event FF802_SIM_MIXER_WRITE=0)
ok "M10: build without mixer writes: port found and reported, no connect, no SysEx, exit 3" \
    '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ] && ! grep -q "^seq connect" "$MIDI" &&
     [ "$(mres outcome)" = disabled-build ] && [ "$(mres midi_port)" = "20:1 Fireface 802 (24047128) Port 2" ] &&
     [ "$(mkind)" = mixer-failed ]'

reset_root; ccdev 19
sed -i "s/24047128) Port 2/24047128) Port 1/" "$R/seq-ports"; mixer on
rc=$(event)
ok "M11: port 1 does not carry the name 'Port 2': refused, nothing sent" \
    '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ] && [ "$(mres outcome)" = refused ]'

for c in yes ON "on extra" ""; do
    reset_root; ccdev 19; printf '%s\n' "$c" > "$STATE/mixer-init"
    rc=$(event)
    ok "M12: mixer-init '$c' counts as off" '[ "$rc" = 0 ] && [ "$(midi_calls)" = 0 ]'
done

reset_root; mixer on; ccdev 19
inst=$(instance $PORT)
pids=""
for i in 1 2 3 4 5; do
    (service "$inst" > "$T/mrc.$i") &
    pids="$pids $!"
done
for p in $pids; do wait "$p"; done
ok "M13: five simultaneous events: serialised, each a complete block of 26, all exit 0" \
    '[ "$(sysex)" = 130 ] && [ "$(cat "$T"/mrc.* | sort -u)" = 0 ] &&
     [ "$(tr "\n" ";" < "$MIDI" | sed -e "s/sysex F0[^;]*;/S/g" | sed "s/seq open;seq list;seq connect 20:1;SSSSSSSSSSSSSSSSSSSSSSSSSSseq close;//g")" = "" ]'

# all 26 messages, written out (independent shell computation, as in
# test_mixer_init_tool.sh)
EXPECTED26="F0 00 20 0D 10 00 00 00 00 11 04 F7
F0 00 20 0D 10 00 00 00 04 13 04 F7
F0 00 20 0D 10 00 00 20 02 01 0C F7
F0 00 20 0D 10 00 00 20 06 03 0C F7
F0 00 20 0D 10 00 00 20 0A 05 0C F7
F0 00 20 0D 10 00 00 20 0E 07 0C F7
F0 00 20 0D 10 00 00 20 12 09 0C F7
F0 00 20 0D 10 00 00 20 16 0B 0C F7
F0 00 20 0D 10 00 00 20 1A 0D 0C F7
F0 00 20 0D 10 00 00 20 1E 0F 0C F7
F0 00 20 0D 10 00 00 20 22 11 0C F7
F0 00 20 0D 10 00 00 20 26 13 0C F7
F0 00 20 0D 10 00 00 20 2A 15 0C F7
F0 00 20 0D 10 00 00 20 2E 17 0C F7
F0 00 20 0D 10 00 00 00 00 70 09 F7
F0 00 20 0D 10 00 00 00 00 78 01 F7
F0 00 20 0D 10 00 00 00 00 00 02 F7
F0 00 20 0D 10 00 00 00 00 08 0A F7
F0 00 20 0D 10 00 00 00 00 10 0A F7
F0 00 20 0D 10 00 00 00 00 18 02 F7
F0 00 20 0D 10 00 00 00 00 20 0A F7
F0 00 20 0D 10 00 00 00 00 28 02 F7
F0 00 20 0D 10 00 00 00 00 30 02 F7
F0 00 20 0D 10 00 00 00 00 38 0A F7
F0 00 20 0D 10 00 00 00 00 40 0A F7
F0 00 20 0D 10 00 00 00 00 48 02 F7"
sent26() { grep '^sysex F0' "$MIDI" | sed 's/^sysex //'; }

reset_root; mixer on; ccdev 19
rc=$(event)
ok "M14: service sends exactly the 26 expected messages, byte for byte, in order" \
    '[ "$rc" = 0 ] && [ "$(sent26)" = "$EXPECTED26" ]'

reset_root; mixer on; ccdev 19
rc=$(event FF802_SIM_SEQ_PORTS_AFTER=2 FF802_SIM_MIXER_TIMEOUT_MS=1000)
ok "M15: MIDI port appears late (after two polls): waited for, then the 26 messages" \
    '[ "$rc" = 0 ] && [ "$(grep -c "^seq list" "$MIDI")" = 3 ] && [ "$(sysex)" = 26 ] && [ "$(mres outcome)" = done ]'
reset_root; mixer on; cc 19
( sleep 0.4; mkdir -p "$DEVS/$PORT/$PORT:1.0/sound/card1/midiC1D0"; seqports 20 ) &
late=$!
rc=$(event FF802_SIM_MIXER_TIMEOUT_MS=3000)
wait "$late"
ok "M15: sound card appears late (after the udev event): waited for, then initialised" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 26 ] && grep -q "^mixer: sound card 1 for $PORT (after [1-9][0-9]* ms)" "$T/journal.txt"'

reset_root; mixer on; ccdev 19
rc=$(event)
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"          # reboot: /run empty, unit still connected
rc2=$(event)                                  # udev coldplug "add" at boot
ok "M16: reboot with the unit connected (coldplug add): initialised again, state of the run dir rebuilt" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(sysex)" = 52 ] && [ "$(mres outcome)" = done ] && [ "$(outcome)" = cc-active ]'
reset_root; mixer on; mode on; prop 18
rc=$(event)
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"          # reboot before reconnecting
unplug; ccdev 19
rc2=$(event)
ok "M16: switched, then reboot instead of reconnect: CC at boot is initialised (no pending needed)" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] && [ "$rc2" = 0 ] && [ "$(sysex)" = 26 ] && [ ! -e "$RUN/event" ]'

reset_root; mode on; mixer off; prop 18
rc=$(event); unplug; ccdev 19; rc2=$(event)
ok "M17: automation on, mixer init off: switch and confirmation as before, mixer untouched" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(writes)" = 1 ] && [ "$(kind)" = cc-active ] &&
     [ "$(midi_calls)" = 0 ] && [ ! -e "$RUN/mixer-result" ]'
reset_root; mode off; mixer off; ccdev 19
rc=$(event)
ok "M17: both off: nothing at all" '[ "$rc" = 0 ] && [ "$(usb_calls)" = 0 ] && [ "$(midi_calls)" = 0 ]'

reset_root; mixer on; ccdev 19
inst=$(instance $PORT); unplug; ccdev 20
rc=$(service "$inst")
ok "M18: stale event (unit reconnected, devnum 19 -> 20): nothing for the stale event, no MIDI access" \
    '[ "$rc" = 3 ] && [ "$(midi_calls)" = 0 ]'
rc=$(event)
ok "M18: the event of the new connection initialises" '[ "$rc" = 0 ] && [ "$(sysex)" = 26 ] && [ "$(mres devnum)" = 20 ]'

reset_root; mixer on; ccdev 19
before=$(cd "$R" && find . | sort)
FF802_AUTO_ROOT=$R "$SIM" --status > "$T/mstatus.txt" 2>&1; rc1=$?
FF802_AUTO_ROOT=$R "$SIM" --mixer-status > "$T/mstatus2.txt" 2>&1; rc2=$?
ok "status and mixer-status are read only and report the mixer initialisation" \
    '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$(cd "$R" && find . | sort)" = "$before" ] &&
     [ "$(midi_calls)" = 0 ] && grep -q "^mixer init: on" "$T/mstatus.txt" &&
     grep -q "sets the 26 registers on the next connection event" "$T/mstatus.txt" &&
     grep -q "^mixer writes in this build: enabled" "$T/mstatus2.txt" &&
     grep -q "are set to 0 dB" "$T/mstatus2.txt"'

# --- guards against unintended use ------------------------------------------------
reset_root; mode on; prop 18
FF802_AUTO_ROOT=$R "$SIM" --service "$(instance $PORT)" > "$T/manual.txt" 2>&1
rc=$?
ok "manual start without systemd (no INVOCATION_ID): refused, nothing read or written" \
    '[ "$rc" = 2 ] && [ "$(usb_calls)" = 0 ] && [ ! -e "$RUN/lock" ] &&
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
ok "installed program of this build: --version reports both write gates, no simulation" \
    'grep -Eq "^ff802-cc-autoswitch [0-9.]+ \(USB writes (enabled|disabled), mixer writes (enabled|disabled|not available \(built without alsa-lib\))\)$" "$T/realv.txt"'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
