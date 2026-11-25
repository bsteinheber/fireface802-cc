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
      FF802_SIM_SEQ_PORTS_AFTER FF802_SIM_REENUM FF802_SIM_REENUM_AFTER FF802_SIM_PORT_STUCK

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

# udev: the unit and instance for an "add" event, from the rule's
# SYSTEMD_WANTS: per port for 2a39:3fcd, per connection otherwise
wants() { sed -n "/^LABEL=\"$1\"\$/,/^LABEL=/s/^ACTION==\"add\", ENV{SYSTEMD_WANTS}+=\"\(.*\)\"\$/\1/p" "$RULE"; }
WANTS_PROP=$(wants ff802_cc_auto_prop)
WANTS_CC=$(wants ff802_cc_auto_cc)
cinst() { # cinst <kernel name>: the instance <k>:<busnum>:<devnum>, zero-padded like udev
    b=$(printf '%03d' "$(cat "$DEVS/$1/busnum")")
    n=$(printf '%03d' "$(cat "$DEVS/$1/devnum")")
    unit=$(printf '%s\n' "$WANTS_CC" | sed -e "s/%k/$1/" -e "s/\$env{BUSNUM}/$b/" -e "s/\$env{DEVNUM}/$n/")
    unit=${unit#ff802-cc-auto@}
    printf '%s\n' "${unit%.service}"
}
instance() { # instance <kernel name>: the instance udev would want for it now
    if [ "$(cat "$DEVS/$1/idVendor")" = 2a39 ] && [ "$(cat "$DEVS/$1/idProduct")" = 3fcd ]; then
        unit=$(printf '%s\n' "$WANTS_PROP" | sed -e "s/%k/$1/")
        unit=${unit#ff802-cc-auto-enable@}
        printf '%s\n' "${unit%.service}"
    else
        cinst "$1"
    fi
}
# systemd: ExecStart of the unit with %i, the program replaced by SIM (both
# units have the same ExecStart, checked below)
ESVC=$SRC/packaging/ff802-cc-auto/ff802-cc-auto-enable@.service
EXEC=$(sed -n 's/^ExecStart=//p' "$SVC")
EXEC_E=$(sed -n 's/^ExecStart=//p' "$ESVC")
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

ok "udev rule and units: proprietary -> enable@<kernel>, CC -> @<kernel>:<busnum>:<devnum>, same ExecStart" \
    'reset_root; prop 19; [ "$(instance $PORT)" = "3-2.4.1.1.3" ] &&
     cc 19; [ "$(instance $PORT)" = "3-2.4.1.1.3:003:019" ] &&
     [ "$EXEC" = "/usr/libexec/ff802-cc/ff802-cc-autoswitch --service %i" ] && [ "$EXEC_E" = "$EXEC" ]'

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
inst=$(cinst $PORT); prop 19
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
old=$(cinst $PORT); prop 19; new=$(cinst $PORT)
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
ok "M2: mixer init on, automation off: 168 SysEx messages to port 20:1 twice (two passes), exit 0, no USB access" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 336 ] && [ "$(usb_calls)" = 0 ] &&
     grep -qx "seq connect 20:1" "$MIDI" && [ "$(mres outcome)" = done ] &&
     [ "$(mres written)" = 168/168 ] && [ "$(mres card)" = 1 ] &&
     [ "$(mres config)" = ff802-mixer-init-v4 ] && [ "$(mres entries)" = 168 ] &&
     [ "$(mres checksum)" = 0xC43D2879 ] && [ "$(mres base)" = ff802-mixer-init-v3 ] &&
     [ "$(mres base_entries)" = 38 ] && [ "$(mres base_checksum)" = 0xEC858A44 ] &&
     grep -qx "mixer: configuration ff802-mixer-init-v4, 168 entries, checksum 0xC43D2879 (verified)." "$T/journal.txt" &&
     grep -qx "mixer: entries 1..38 are ff802-mixer-init-v3, checksum 0xEC858A44 (verified)." "$T/journal.txt" &&
     grep -qx "mixer: entries 1..26 are ff802-mixer-init-v2, checksum 0xC97A359C (verified)." "$T/journal.txt" &&
     [ "$(mres midi_port)" = "20:1 Fireface 802 (24047128) Port 2" ] && [ ! -e "$RUN/mixer-event" ]'
ok "M2: order: the v3 part (crossed off, routings, volumes, Input 9), then the 130 other crossed routings off" \
    '[ "$(nth_sysex 1)" = "sysex F0 00 20 0D 10 00 00 00 00 11 04 F7" ] &&
     [ "$(nth_sysex 2)" = "sysex F0 00 20 0D 10 00 00 00 04 13 04 F7" ] &&
     [ "$(nth_sysex 3)" = "sysex F0 00 20 0D 10 00 00 20 02 01 0C F7" ] &&
     [ "$(nth_sysex 4)" = "sysex F0 00 20 0D 10 00 00 20 06 03 0C F7" ] &&
     [ "$(nth_sysex 11)" = "sysex F0 00 20 0D 10 00 00 20 22 11 0C F7" ] &&
     [ "$(nth_sysex 23)" = "sysex F0 00 20 0D 10 00 00 00 00 30 02 F7" ] &&
     [ "$(nth_sysex 27)" = "sysex F0 00 20 0D 10 00 66 4C 20 00 04 F7" ] &&
     [ "$(nth_sysex 29)" = "sysex F0 00 20 0D 10 00 66 4C 20 04 0C F7" ] &&
     [ "$(nth_sysex 38)" = "sysex F0 00 20 0D 10 00 66 4C 20 16 0C F7" ] &&
     [ "$(nth_sysex 39)" = "sysex F0 00 20 0D 10 00 00 00 04 01 04 F7" ] &&
     [ "$(nth_sysex 149)" = "sysex F0 00 20 0D 10 00 00 00 08 15 04 F7" ] &&
     [ "$(nth_sysex 168)" = "sysex F0 00 20 0D 10 00 00 00 28 17 04 F7" ] &&
     [ "$(tr "\n" ";" < "$MIDI" | sed "s/sysex F0[^;]*;//g")" = "seq open;seq list;seq connect 20:1;seq close;" ]'
ok "M2: one journal line per register, last-result stays cc-active" \
    '[ "$(grep -c "^mixer: set 0x" "$T/journal.txt")" = 168 ] &&
     grep -q "^mixer: set 0x4228 = 0x9000 (Playback 9 -> Phones 9, 0 dB) \[11/168\]$" "$T/journal.txt" &&
     grep -q "^mixer: set 0x2900 = 0x0000 (Output Phones 12 volume, 0 dB) \[26/168\]$" "$T/journal.txt" &&
     grep -q "^mixer: set 0x4088 = 0x2666 (Input 9 -> Analog 3, 30 %) \[29/168\]$" "$T/journal.txt" &&
     [ "$(outcome)" = cc-active ] && grep -q "mixer initialisation: done" "$RUN/last-result"'
ok "M2: the service sends the whole list twice: pass 2 after the pause, marked in the journal, result 'twice'" \
    '[ "$(grep -c "^mixer: pass 2: set 0x" "$T/journal.txt")" = 168 ] &&
     grep -qx "mixer: pass 1 of 2 complete; pass 2 (the same 168 values) in 20 ms." "$T/journal.txt" &&
     grep -q "^mixer: pass 2: set 0x4228 = 0x9000 (Playback 9 -> Phones 9, 0 dB) \[11/168\]$" "$T/journal.txt" &&
     [ "$(grep "^sysex F0" "$MIDI" | head -n 168)" = "$(grep "^sysex F0" "$MIDI" | tail -n 168)" ] &&
     [ "$(mres written)" = 168/168 ] && grep -q "^detail=168 registers sent to MIDI port 20:1 .*, twice$" "$RUN/mixer-result" &&
     [ "$(tr "\n" ";" < "$MIDI" | grep -o "seq connect" | wc -l)" = 1 ]'

rc=$(event)
ok "M3: a repeated add event of the same connection initialises again (same values), exit 0" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 672 ] && [ "$(nth_sysex 337)" = "$(nth_sysex 1)" ] && [ "$(nth_sysex 169)" = "$(nth_sysex 1)" ]'
unplug; ccdev 20 24
rc=$(event)
ok "M3: new connection, client number changed (24): found by card, initialised" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 1008 ] && grep -qx "seq connect 24:1" "$MIDI"'
unplug; ccdev 19
rc=$(event)
ok "M4: reused device number (19 again): the initialisation is never suppressed" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 1344 ] && [ "$(mres devnum)" = 19 ] && [ "$(mres outcome)" = done ]'

reset_root; mode on; mixer on; prop 18
rc=$(event)
ok "M5: proprietary mode: only the switch, no MIDI access" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] && [ "$(midi_calls)" = 0 ] && [ ! -e "$RUN/mixer-result" ]'
unplug; ccdev 19
rc=$(event)
ok "M5: after the switch, reconnected in CC mode: cc-active event and mixer done, kept apart" \
    '[ "$rc" = 0 ] && [ "$(kind)" = cc-active ] && [ "$(sysex)" = 336 ] && [ ! -e "$RUN/mixer-event" ] &&
     [ "$(mres outcome)" = done ]'

reset_root; mixer on; cc 19
rc=$(event)
ok "M6: no sound card within the timeout: exit 3, no-port, sequencer never opened, mixer-failed event" \
    '[ "$rc" = 3 ] && [ "$(midi_calls)" = 0 ] && [ "$(mres outcome)" = no-port ] &&
     [ "$(mkind)" = mixer-failed ] && grep -q "^detail=.*no sound card with MIDI device.*(0 of 168 registers)" "$RUN/mixer-event" &&
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
    '[ "$rc" = 4 ] && [ "$(sysex)" = 6 ] && [ "$(mres outcome)" = incomplete ] && [ "$(mres written)" = 6/168 ] &&
     [ "$(grep -c "^sysex FAILED" "$MIDI")" = 1 ] && grep -q "(6 of 168 registers)" "$RUN/mixer-event" &&
     grep -q "seq close" "$MIDI"'

reset_root; mixer on; ccdev 19
rc=$(event FF802_SIM_SEQ_SEND_ERR_AT=175)
ok "M9: sending fails in pass 2 (register 7 again): exit 4, incomplete, pass 1 complete, nothing retried" \
    '[ "$rc" = 4 ] && [ "$(sysex)" = 174 ] && [ "$(mres outcome)" = incomplete ] && [ "$(mres written)" = 168/168 ] &&
     grep -q "pass 2: sending register 7 of 168 (0x4124) failed" "$RUN/mixer-result" &&
     grep -q "^mixer: pass 2: FAILED 0x4124" "$T/journal.txt" && [ "$(mkind)" = mixer-failed ]'

reset_root; mixer on; ccdev 19
( sleep 0.7; unplug ) &
gone=$!
rc=$(event FF802_SIM_MIXER_REPEAT_MS=2000)
wait "$gone"
ok "M19: unit gone during the pause: pass 2 skipped, pass 1 counts, done, exit 0" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 168 ] && [ "$(mres outcome)" = done ] &&
     grep -q "^mixer: pass 2 skipped: " "$T/journal.txt" && grep -q "pass 2 skipped" "$RUN/mixer-result"'

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
ok "M13: five simultaneous events: serialised, each a complete block of 2 x 168, all exit 0" \
    '[ "$(sysex)" = 1680 ] && [ "$(cat "$T"/mrc.* | sort -u)" = 0 ] &&
     [ "$(tr "\n" ";" < "$MIDI" | sed -e "s/sysex F0[^;]*;/S/g" | sed "s/seq open;seq list;seq connect 20:1;SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSseq close;//g")" = "" ]'

# all 168 messages, written out (independent shell computation, as in
# test_mixer_init_tool.sh)
EXPECTED_ALL="F0 00 20 0D 10 00 00 00 00 11 04 F7
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
F0 00 20 0D 10 00 00 00 00 48 02 F7
F0 00 20 0D 10 00 66 4C 20 00 04 F7
F0 00 20 0D 10 00 66 4C 20 02 0C F7
F0 00 20 0D 10 00 66 4C 20 04 0C F7
F0 00 20 0D 10 00 66 4C 20 06 04 F7
F0 00 20 0D 10 00 66 4C 20 08 0C F7
F0 00 20 0D 10 00 66 4C 20 0A 04 F7
F0 00 20 0D 10 00 66 4C 20 0C 04 F7
F0 00 20 0D 10 00 66 4C 20 0E 0C F7
F0 00 20 0D 10 00 66 4C 20 10 0C F7
F0 00 20 0D 10 00 66 4C 20 12 04 F7
F0 00 20 0D 10 00 66 4C 20 14 04 F7
F0 00 20 0D 10 00 66 4C 20 16 0C F7
F0 00 20 0D 10 00 00 00 04 01 04 F7
F0 00 20 0D 10 00 00 00 08 01 04 F7
F0 00 20 0D 10 00 00 00 0C 01 0C F7
F0 00 20 0D 10 00 00 00 10 01 04 F7
F0 00 20 0D 10 00 00 00 14 01 0C F7
F0 00 20 0D 10 00 00 00 18 01 0C F7
F0 00 20 0D 10 00 00 00 1C 01 04 F7
F0 00 20 0D 10 00 00 00 20 01 04 F7
F0 00 20 0D 10 00 00 00 24 01 0C F7
F0 00 20 0D 10 00 00 00 28 01 0C F7
F0 00 20 0D 10 00 00 00 2C 01 04 F7
F0 00 20 0D 10 00 00 00 00 03 04 F7
F0 00 20 0D 10 00 00 00 08 03 0C F7
F0 00 20 0D 10 00 00 00 0C 03 04 F7
F0 00 20 0D 10 00 00 00 10 03 0C F7
F0 00 20 0D 10 00 00 00 14 03 04 F7
F0 00 20 0D 10 00 00 00 18 03 04 F7
F0 00 20 0D 10 00 00 00 1C 03 0C F7
F0 00 20 0D 10 00 00 00 20 03 0C F7
F0 00 20 0D 10 00 00 00 24 03 04 F7
F0 00 20 0D 10 00 00 00 28 03 04 F7
F0 00 20 0D 10 00 00 00 2C 03 0C F7
F0 00 20 0D 10 00 00 00 00 05 04 F7
F0 00 20 0D 10 00 00 00 04 05 0C F7
F0 00 20 0D 10 00 00 00 0C 05 04 F7
F0 00 20 0D 10 00 00 00 10 05 0C F7
F0 00 20 0D 10 00 00 00 14 05 04 F7
F0 00 20 0D 10 00 00 00 18 05 04 F7
F0 00 20 0D 10 00 00 00 1C 05 0C F7
F0 00 20 0D 10 00 00 00 20 05 0C F7
F0 00 20 0D 10 00 00 00 24 05 04 F7
F0 00 20 0D 10 00 00 00 28 05 04 F7
F0 00 20 0D 10 00 00 00 2C 05 0C F7
F0 00 20 0D 10 00 00 00 00 07 0C F7
F0 00 20 0D 10 00 00 00 04 07 04 F7
F0 00 20 0D 10 00 00 00 08 07 04 F7
F0 00 20 0D 10 00 00 00 10 07 04 F7
F0 00 20 0D 10 00 00 00 14 07 0C F7
F0 00 20 0D 10 00 00 00 18 07 0C F7
F0 00 20 0D 10 00 00 00 1C 07 04 F7
F0 00 20 0D 10 00 00 00 20 07 04 F7
F0 00 20 0D 10 00 00 00 24 07 0C F7
F0 00 20 0D 10 00 00 00 28 07 0C F7
F0 00 20 0D 10 00 00 00 2C 07 04 F7
F0 00 20 0D 10 00 00 00 00 09 04 F7
F0 00 20 0D 10 00 00 00 04 09 0C F7
F0 00 20 0D 10 00 00 00 08 09 0C F7
F0 00 20 0D 10 00 00 00 0C 09 04 F7
F0 00 20 0D 10 00 00 00 14 09 04 F7
F0 00 20 0D 10 00 00 00 18 09 04 F7
F0 00 20 0D 10 00 00 00 1C 09 0C F7
F0 00 20 0D 10 00 00 00 20 09 0C F7
F0 00 20 0D 10 00 00 00 24 09 04 F7
F0 00 20 0D 10 00 00 00 28 09 04 F7
F0 00 20 0D 10 00 00 00 2C 09 0C F7
F0 00 20 0D 10 00 00 00 00 0B 0C F7
F0 00 20 0D 10 00 00 00 04 0B 04 F7
F0 00 20 0D 10 00 00 00 08 0B 04 F7
F0 00 20 0D 10 00 00 00 0C 0B 0C F7
F0 00 20 0D 10 00 00 00 10 0B 04 F7
F0 00 20 0D 10 00 00 00 18 0B 0C F7
F0 00 20 0D 10 00 00 00 1C 0B 04 F7
F0 00 20 0D 10 00 00 00 20 0B 04 F7
F0 00 20 0D 10 00 00 00 24 0B 0C F7
F0 00 20 0D 10 00 00 00 28 0B 0C F7
F0 00 20 0D 10 00 00 00 2C 0B 04 F7
F0 00 20 0D 10 00 00 00 00 0D 0C F7
F0 00 20 0D 10 00 00 00 04 0D 04 F7
F0 00 20 0D 10 00 00 00 08 0D 04 F7
F0 00 20 0D 10 00 00 00 0C 0D 0C F7
F0 00 20 0D 10 00 00 00 10 0D 04 F7
F0 00 20 0D 10 00 00 00 14 0D 0C F7
F0 00 20 0D 10 00 00 00 1C 0D 04 F7
F0 00 20 0D 10 00 00 00 20 0D 04 F7
F0 00 20 0D 10 00 00 00 24 0D 0C F7
F0 00 20 0D 10 00 00 00 28 0D 0C F7
F0 00 20 0D 10 00 00 00 2C 0D 04 F7
F0 00 20 0D 10 00 00 00 00 0F 04 F7
F0 00 20 0D 10 00 00 00 04 0F 0C F7
F0 00 20 0D 10 00 00 00 08 0F 0C F7
F0 00 20 0D 10 00 00 00 0C 0F 04 F7
F0 00 20 0D 10 00 00 00 10 0F 0C F7
F0 00 20 0D 10 00 00 00 14 0F 04 F7
F0 00 20 0D 10 00 00 00 18 0F 04 F7
F0 00 20 0D 10 00 00 00 20 0F 0C F7
F0 00 20 0D 10 00 00 00 24 0F 04 F7
F0 00 20 0D 10 00 00 00 28 0F 04 F7
F0 00 20 0D 10 00 00 00 2C 0F 0C F7
F0 00 20 0D 10 00 00 00 04 11 0C F7
F0 00 20 0D 10 00 00 00 08 11 0C F7
F0 00 20 0D 10 00 00 00 0C 11 04 F7
F0 00 20 0D 10 00 00 00 10 11 0C F7
F0 00 20 0D 10 00 00 00 14 11 04 F7
F0 00 20 0D 10 00 00 00 18 11 04 F7
F0 00 20 0D 10 00 00 00 1C 11 0C F7
F0 00 20 0D 10 00 00 00 24 11 04 F7
F0 00 20 0D 10 00 00 00 28 11 04 F7
F0 00 20 0D 10 00 00 00 2C 11 0C F7
F0 00 20 0D 10 00 00 00 00 13 0C F7
F0 00 20 0D 10 00 00 00 08 13 04 F7
F0 00 20 0D 10 00 00 00 0C 13 0C F7
F0 00 20 0D 10 00 00 00 10 13 04 F7
F0 00 20 0D 10 00 00 00 14 13 0C F7
F0 00 20 0D 10 00 00 00 18 13 0C F7
F0 00 20 0D 10 00 00 00 1C 13 04 F7
F0 00 20 0D 10 00 00 00 20 13 04 F7
F0 00 20 0D 10 00 00 00 28 13 0C F7
F0 00 20 0D 10 00 00 00 2C 13 04 F7
F0 00 20 0D 10 00 00 00 00 15 0C F7
F0 00 20 0D 10 00 00 00 04 15 04 F7
F0 00 20 0D 10 00 00 00 08 15 04 F7
F0 00 20 0D 10 00 00 00 0C 15 0C F7
F0 00 20 0D 10 00 00 00 10 15 04 F7
F0 00 20 0D 10 00 00 00 14 15 0C F7
F0 00 20 0D 10 00 00 00 18 15 0C F7
F0 00 20 0D 10 00 00 00 1C 15 04 F7
F0 00 20 0D 10 00 00 00 20 15 04 F7
F0 00 20 0D 10 00 00 00 24 15 0C F7
F0 00 20 0D 10 00 00 00 2C 15 04 F7
F0 00 20 0D 10 00 00 00 00 17 04 F7
F0 00 20 0D 10 00 00 00 04 17 0C F7
F0 00 20 0D 10 00 00 00 08 17 0C F7
F0 00 20 0D 10 00 00 00 0C 17 04 F7
F0 00 20 0D 10 00 00 00 10 17 0C F7
F0 00 20 0D 10 00 00 00 14 17 04 F7
F0 00 20 0D 10 00 00 00 18 17 04 F7
F0 00 20 0D 10 00 00 00 1C 17 0C F7
F0 00 20 0D 10 00 00 00 20 17 0C F7
F0 00 20 0D 10 00 00 00 24 17 04 F7
F0 00 20 0D 10 00 00 00 28 17 04 F7"
sent_all() { grep '^sysex F0' "$MIDI" | sed 's/^sysex //'; }
EXPECTED_TWICE=$(printf '%s\n%s' "$EXPECTED_ALL" "$EXPECTED_ALL")

reset_root; mixer on; ccdev 19
rc=$(event)
ok "M14: service sends exactly the 168 expected messages, byte for byte, in order, twice" \
    '[ "$rc" = 0 ] && [ "$(sent_all)" = "$EXPECTED_TWICE" ]'

reset_root; mixer on; ccdev 19
rc=$(event FF802_SIM_SEQ_PORTS_AFTER=2 FF802_SIM_MIXER_TIMEOUT_MS=1000)
ok "M15: MIDI port appears late (after two polls): waited for, then the 168 messages" \
    '[ "$rc" = 0 ] && [ "$(grep -c "^seq list" "$MIDI")" = 3 ] && [ "$(sysex)" = 336 ] && [ "$(mres outcome)" = done ]'
reset_root; mixer on; cc 19
( sleep 0.4; mkdir -p "$DEVS/$PORT/$PORT:1.0/sound/card1/midiC1D0"; seqports 20 ) &
late=$!
rc=$(event FF802_SIM_MIXER_TIMEOUT_MS=3000)
wait "$late"
ok "M15: sound card appears late (after the udev event): waited for, then initialised" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 336 ] && grep -q "^mixer: sound card 1 for $PORT (after [1-9][0-9]* ms)" "$T/journal.txt"'

reset_root; mixer on; ccdev 19
rc=$(event)
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"          # reboot: /run empty, unit still connected
rc2=$(event)                                  # udev coldplug "add" at boot
ok "M16: reboot with the unit connected (coldplug add): initialised again, state of the run dir rebuilt" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(sysex)" = 672 ] && [ "$(mres outcome)" = done ] && [ "$(outcome)" = cc-active ]'
reset_root; mixer on; mode on; prop 18
rc=$(event)
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"          # reboot before reconnecting
unplug; ccdev 19
rc2=$(event)
ok "M16: switched, then reboot instead of reconnect: CC at boot is initialised (no pending needed)" \
    '[ "$rc" = 0 ] && [ "$(writes)" = 1 ] && [ "$rc2" = 0 ] && [ "$(sysex)" = 336 ] && [ ! -e "$RUN/event" ]'

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
ok "M18: the event of the new connection initialises" '[ "$rc" = 0 ] && [ "$(sysex)" = 336 ] && [ "$(mres devnum)" = 20 ]'

reset_root; mixer on; ccdev 19
before=$(cd "$R" && find . | sort)
FF802_AUTO_ROOT=$R "$SIM" --status > "$T/mstatus.txt" 2>&1; rc1=$?
FF802_AUTO_ROOT=$R "$SIM" --mixer-status > "$T/mstatus2.txt" 2>&1; rc2=$?
ok "status and mixer-status are read only and report the mixer initialisation" \
    '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$(cd "$R" && find . | sort)" = "$before" ] &&
     [ "$(midi_calls)" = 0 ] && grep -q "^mixer init: on" "$T/mstatus.txt" &&
     grep -q "sets the ff802-mixer-init-v4 registers on the next connection event" "$T/mstatus.txt" &&
     grep -q "^mixer writes in this build: enabled" "$T/mstatus2.txt" &&
     grep -q "are set to 0 dB" "$T/mstatus2.txt"'

# --- automatic port reset after the command (ff802_port_switch) ----------------------
HUB=3-2.4.1.1
HUBPORT=$DEVS/$HUB/$HUB:1.0/$HUB-port3
hubport() { # the hub port of $PORT: "disable" and the "device" link to the device
    mkdir -p "$HUBPORT"
    echo 0 > "$HUBPORT/disable"
    ln -s "../../../$PORT" "$HUBPORT/device"
}
disable_val() { cat "$HUBPORT/disable" 2>/dev/null; }
resets() { n=$(grep -c '^port off ' "$R/usb.log" 2>/dev/null); echo "${n:-0}"; }
jl() { grep -c "$1" "$T/journal.txt"; }

reset_root; : > "$T/journal.txt"; mode on; prop 18; hubport
rc=$(event FF802_SIM_REENUM=cc)
ok "P1: proprietary, port resettable: command, then disable=1 / 2000 ms / disable=0, back as 0424:3fdd" \
    '[ "$rc" = 0 ] && [ "$(outcome)" = switched ] && [ "$(writes)" = 1 ] && [ "$(resets)" = 1 ] &&
     [ "$(grep -v "^list\|^layout" "$R/usb.log" | tr "\n" ";")" = "init;open 3:18;kdrv if 1;claim if 1;write ep=0x0C bytes=01 00 44 BD bus=3 addr=18 timeout=1000;release if 1;close;exit;port off 2000 ms disable=1;port on disable=0;sim: $PORT re-enumerated (cc) as devnum 19 after 3 polls;" ] &&
     [ "$(disable_val)" = 0 ] && [ "$(cat "$DEVS/$PORT/idVendor")" = 0424 ] && [ "$(cat "$DEVS/$PORT/devnum")" = 19 ]'
ok "P1: journal: command sent, reset triggered, re-enumerated in CC mode; no notification yet, pending set" \
    '[ "$(jl "^CC command sent: 01 00 44 BD to $PORT (bus 3, devnum 18), 4 bytes.$")" = 1 ] &&
     [ "$(jl "^Port reset triggered: .*/$HUB-port3/disable = 1, 2000 ms, = 0; waiting up to 15000 ms for 0424:3fdd at $PORT.$")" = 1 ] &&
     [ "$(jl "^Re-enumerated in class-compliant mode: $PORT 0424:3fdd bus 3 devnum 19 (was devnum 18)")" = 1 ] &&
     [ "$(jl "^Result: switched: switched $PORT: command sent, port reset, back as 0424:3fdd devnum 19$")" = 1 ] &&
     [ ! -f "$RUN/event" ] && [ -f "$RUN/pending" ] && [ ! -e "$RUN/reset-blocked" ] &&
     [ "$(jl "reconnect")" = 0 ]'
mixer on; sndcard; seqports 20
rc=$(event)
ok "P1: the new connection (own instance, devnum 19): cc-active confirmation, pending cleared, 168 registers" \
    '[ "$rc" = 0 ] && [ "$(outcome)" = cc-active ] && [ "$(kind)" = cc-active ] && [ ! -f "$RUN/pending" ] &&
     [ "$(sysex)" = 336 ] && [ "$(mres outcome)" = done ] && [ "$(mres devnum)" = 19 ] &&
     [ "$(mres checksum)" = 0xC43D2879 ] && [ "$(mres base_checksum)" = 0xEC858A44 ] && [ "$(writes)" = 1 ] && [ "$(resets)" = 1 ]'

reset_root; : > "$T/journal.txt"; mode on; prop 18; hubport
rc=$(event FF802_SIM_REENUM=none)
ok "P2: device not re-enumerated within 15 s: exit 5, 'not-reenumerated', event reset-failed, reset blocked" \
    '[ "$rc" = 5 ] && [ "$(outcome)" = not-reenumerated ] && [ "$(kind)" = reset-failed ] &&
     [ "$(resets)" = 1 ] && [ "$(disable_val)" = 0 ] && [ -f "$RUN/reset-blocked" ] &&
     [ "$(jl "^Port reset done, device not re-enumerated at $PORT within 15000 ms.$")" = 1 ] &&
     grep -q "^detail=command sent, port reset, device not back within 15 s; reconnect the USB cable$" "$RUN/event"'
ok "P2: nothing retried: one command, one reset" \
    '[ "$(writes)" = 1 ] && [ "$(jl "^Port reset triggered")" = 1 ]'

reset_root; : > "$T/journal.txt"; mode on; prop 18; hubport
rc=$(event FF802_SIM_REENUM=prop)
ok "P3: back in proprietary mode (new devnum): exit 5, 'reenumerated-not-cc', reset blocked" \
    '[ "$rc" = 5 ] && [ "$(outcome)" = reenumerated-not-cc ] && [ "$(kind)" = reset-failed ] &&
     [ -f "$RUN/reset-blocked" ] &&
     [ "$(jl "^Port reset done, re-enumerated at $PORT as 2a39:3fcd devnum 19: not class-compliant mode.$")" = 1 ]'
rc=$(event FF802_SIM_REENUM=prop)
rc2=$(event FF802_SIM_REENUM=prop)
ok "P3: no retry loop: the new connection gets the command once, no second reset, asked to reconnect" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(writes)" = 2 ] && [ "$(resets)" = 1 ] &&
     [ "$(outcome)" = already-handled ] && [ "$(kind)" = prepared ] &&
     [ "$(jl "^No automatic port reset: an automatic port reset failed earlier on this boot")" = 1 ]'
FF802_AUTO_ROOT=$R "$SRC/packaging/ff802-cc-auto/ff802-cc-auto-on" > /dev/null 2>&1
ok "P3: 'ff802-cc-auto on' lifts the reset block" '[ ! -e "$RUN/reset-blocked" ]'
rm -rf -- "${RUN:?}"; mkdir -p "$RUN"; mode on
unplug; prop 20
rc=$(event FF802_SIM_REENUM=cc)
ok "P3: after a reboot (/run empty) a new connection is reset again" \
    '[ "$rc" = 0 ] && [ "$(outcome)" = switched ] && [ "$(resets)" = 2 ]'

reset_root; : > "$T/journal.txt"; mode on; prop 18; hubport
rc=$(event FF802_SIM_REENUM=cc FF802_SIM_PORT_STUCK=1)
ok "P4: disable=0 fails: exit 5, 'reset-failed', hint that the port may stay disabled, reset blocked" \
    '[ "$rc" = 5 ] && [ "$(outcome)" = reset-failed ] && [ "$(kind)" = reset-failed ] &&
     [ -f "$RUN/reset-blocked" ] &&
     [ "$(jl "^Port reset failed: writing 0 to .*/$HUB-port3/disable failed (Is a directory); the port may stay disabled. Re-enable it: echo 0 | sudo tee .*/$HUB-port3/disable >/dev/null$")" = 1 ]'

reset_root; : > "$T/journal.txt"; mode on; prop 18; hubport; chmod 0444 "$HUBPORT/disable"
rc=$(event FF802_SIM_REENUM=cc)
ok "P5: port not writable for the service (e.g. read-only /sys): command sent, no reset, asked to reconnect" \
    '[ "$rc" = 0 ] && [ "$(outcome)" = sent ] && [ "$(kind)" = prepared ] && [ "$(writes)" = 1 ] &&
     [ "$(resets)" = 0 ] && [ "$(disable_val)" = 0 ] && [ ! -e "$RUN/reset-blocked" ] &&
     [ "$(jl "^No automatic port reset: hub port $HUB-port3 not writable for this service.")" = 1 ] &&
     grep -q "^detail=enable command transferred to $PORT (4 bytes); no automatic port reset (hub port $HUB-port3 not writable for this service): reconnect the USB cable$" "$RUN/event"'
chmod 0644 "$HUBPORT/disable"

reset_root; : > "$T/journal.txt"; mode on; prop 18; mkdir -p "$HUBPORT"; echo 0 > "$HUBPORT/disable"
ln -s "../../../3-2.4.1.1.2" "$HUBPORT/device"; dev 3-2.4.1.1.2 0424 2514 3 9 "USB2514B" "Microchip"
rc=$(event FF802_SIM_REENUM=cc)
ok "P6: the port's device link is another device: never written, command sent, asked to reconnect" \
    '[ "$rc" = 0 ] && [ "$(outcome)" = sent ] && [ "$(resets)" = 0 ] && [ "$(disable_val)" = 0 ] &&
     [ "$(jl "^No automatic port reset: ")" = 1 ]'

reset_root; : > "$T/journal.txt"; prop 18; hubport
rc=$(event FF802_SIM_REENUM=cc)
mode on; cc 19
rc2=$(event)
ok "P7: automation off: no command, no reset; a CC device is never reset" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(usb_calls)" = 0 ] && [ "$(disable_val)" = 0 ]'

reset_root; : > "$T/journal.txt"; mode on; prop 18; hubport; mixer on; sndcard; seqports 20
(service "$(instance $PORT)" FF802_SIM_REENUM=cc FF802_SIM_WRITE_DELAY_MS=400 > "$T/rc.enable") & p1=$!
sleep 0.2
cc_inst="$PORT:003:019"       # the event of the re-enumerated device, while the lock is held
(service "$cc_inst" > "$T/rc.cc") & p2=$!
wait $p1; wait $p2
ok "P8: CC event during the reset waits for the lock, then confirms and initialises; no deadlock" \
    '[ "$(cat "$T/rc.enable")" = 0 ] && [ "$(cat "$T/rc.cc")" = 0 ] && [ "$(kind)" = cc-active ] &&
     [ "$(sysex)" = 336 ] && [ "$(mres devnum)" = 19 ] &&
     [ "$(sed -n "s/^Result: \([a-z-]*\):.*/\1/p" "$T/journal.txt" | tr "\n" ";")" = "switched;cc-active;" ]'

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
