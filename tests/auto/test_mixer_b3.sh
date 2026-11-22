#!/bin/sh
# Hardware test program B.3 (ff802-mixer-b3), without hardware:
#  - the simulation build (simulated MIDI backend, fake sysfs below a
#    private root): detection, port by card number with changing client
#    numbers, the one message, confirmation, every refusal;
#  - the real program, if built (FF802_BUILD_MIXER_B3=ON): only --help,
#    --version and usage errors (nothing that reaches sysfs or ALSA), and
#    its linked symbols with nm;
#  - the source: exactly the one message 0x4228 = 0x9000.
#
# Usage: test_mixer_b3.sh <source dir> <sim program> <real program or -> <nm> <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
SIM=$2
REAL=$3
NM=$4
T=$5
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
    */b3-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
R=$T/root
PORT=3-2.4.1.1.3
DEVS=$R/sys/bus/usb/devices
MIDI=$R/midi.log
unset FF802_SIM_SEQ_OPEN_ERR FF802_SIM_SEQ_LIST_ERR FF802_SIM_SEQ_CONNECT_ERR \
      FF802_SIM_SEQ_SEND_ERR_AT FF802_SIM_MIXER_WRITE FF802_SIM_MIXER_TIMEOUT_MS

MSG="F0 00 20 0D 10 00 00 20 22 11 0C F7"

dev() { # dev <name> <vid> <pid> <devnum>
    d=$DEVS/$1; mkdir -p "$d"
    echo "$2" > "$d/idVendor"; echo "$3" > "$d/idProduct"; echo 3 > "$d/busnum"
    echo "$4" > "$d/devnum"; echo 480 > "$d/speed"; echo RME > "$d/manufacturer"
    echo "Fireface 802 (24047128)" > "$d/product"
}
card() { mkdir -p "$DEVS/$1/$1:1.0/sound/card$2/midiC$2D0"; }
seqports() { # seqports <client> [<name of port 1>]
    cat > "$R/seq-ports" <<EOF
0 0 kernel -1 - Timer
14 0 kernel -1 w Midi Through Port-0
$1 0 kernel 1 w Fireface 802 (24047128) Port 1
$1 1 kernel 1 w ${2:-Fireface 802 (24047128) Port 2}
144 0 user -1 w input
EOF
}
reset() { # the host of B.1/B.2: one unit in CC mode, card 1, client <n>
    rm -rf -- "${R:?}"
    mkdir -p "$DEVS"
    dev "$PORT" 0424 3fdd 53; card "$PORT" 1; seqports "${1:-20}"
}
b3() { # b3 <stdin text> <args...>: print the exit code
    input=$1; shift
    printf '%s\n' "$input" | env FF802_AUTO_ROOT="$R" "$SIM" "$@" > "$T/out.txt" 2>&1
    echo $?
}
sysex() { n=$(grep -c '^sysex ' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
connects() { n=$(grep -c '^seq connect' "$MIDI" 2>/dev/null); echo "${n:-0}"; }

# --- dry run ------------------------------------------------------------------------
reset 20
rc=$(b3 "" --dry-run)
ok "dry run (B.1 host): unit, card 1, port 20:1 selected, nothing sent" \
    '[ "$rc" = 0 ] && grep -q "^identity: Fireface 802, class-compliant mode" "$T/out.txt" &&
     grep -q "^sound card (sysfs): found, card 1" "$T/out.txt" &&
     grep -q "^selected port: 20:1 .Fireface 802 (24047128) Port 2.$" "$T/out.txt" &&
     grep -q "^dry run: nothing sent" "$T/out.txt" &&
     [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq close;" ]'
ok "dry run lists exactly the one message 0x4228 = 0x9000" \
    '[ "$(grep -c "^message " "$T/out.txt")" = 1 ] &&
     grep -q "^message route 0x4228 = 0x9000  Playback 9 -> Hardware Output 9 (Phones 9) 0 dB$" "$T/out.txt" &&
     grep -q "^              $MSG$" "$T/out.txt"'
for c in 24 128 7; do
    reset "$c"
    rc=$(b3 "" --dry-run)
    ok "changing sequencer client number $c: found by card number" \
        '[ "$rc" = 0 ] && grep -q "^selected port: $c:1 " "$T/out.txt"'
done
reset 20
rc=$(b3 "" --dry-run --connection "$PORT")
ok "dry run with --connection" '[ "$rc" = 0 ] && grep -q "^selected port: 20:1 " "$T/out.txt"'

# --- send ----------------------------------------------------------------------------
reset 24
rc=$(b3 SEND --send)
ok "send: confirmed, connect to 24:1, exactly ONE message, 0x4228 = 0x9000" \
    '[ "$rc" = 0 ] && [ "$(connects)" = 1 ] && grep -qx "seq connect 24:1" "$MIDI" &&
     [ "$(sysex)" = 1 ] && grep -qx "sysex $MSG" "$MIDI" &&
     [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq list;seq connect 24:1;sysex $MSG;seq close;" ] &&
     grep -q "sent 12 bytes" "$T/out.txt"'
ok "send: the same bytes as entry 9 of the mixer initialisation (Playback 9 -> Phones 9)" \
    'grep -q "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x22, 0x11, 0x0C, 0xF7 }" "$SRC/tools/hw-test/ff802_mixer_b3.c" &&
     grep -q "R(9, 0x4228)" "$SRC/src/mixer_init.c"'

for answer in "" "send" "yes" "SEND " "SENDX"; do
    reset 20
    rc=$(b3 "$answer" --send)
    ok "confirmation '$answer': aborted, no connect, nothing sent" \
        '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && grep -q "aborted: nothing sent" "$T/out.txt"'
done

reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 b3 SEND --send)
ok "program without write functions: refused before asking, nothing sent" \
    '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && grep -q "not compiled into this program" "$T/out.txt" &&
     ! grep -q "Type SEND" "$T/out.txt"'
reset 20
rc=$(FF802_SIM_SEQ_CONNECT_ERR=busy b3 SEND --send)
ok "connect fails (busy): exit 3, nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 b3 SEND --send)
ok "sending fails: exit 4, one attempt, no retry" \
    '[ "$rc" = 4 ] && [ "$(grep -c "^sysex" "$MIDI")" = 1 ] && grep -q "nothing retried" "$T/out.txt"'

# --- restore stereo routing 9/10 ---------------------------------------------------
MSG10="F0 00 20 0D 10 00 00 20 26 13 0C F7"
reset 20
rc=$(b3 "" --restore-stereo --dry-run)
ok "restore-stereo dry run: both messages listed in order, port found, nothing sent" \
    '[ "$rc" = 0 ] && [ "$(grep -c "^message " "$T/out.txt")" = 2 ] &&
     [ "$(grep "^message " "$T/out.txt" | tr "\n" ";")" = "message stereo-9 0x4228 = 0x9000  Playback 9 -> Hardware Output 9 (Phones 9) 0 dB;message stereo-10 0x4269 = 0x9000  Playback 10 -> Hardware Output 10 (Phones 10) 0 dB;" ] &&
     grep -q "^              $MSG$" "$T/out.txt" && grep -q "^              $MSG10$" "$T/out.txt" &&
     grep -q "^dry run: nothing sent" "$T/out.txt" && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ]'
rc=$(b3 "" --dry-run --restore-stereo)
ok "restore-stereo dry run: argument order does not matter" '[ "$rc" = 0 ] && [ "$(sysex)" = 0 ]'

reset 24
rc=$(b3 SEND --restore-stereo)
ok "restore-stereo: exactly TWO messages, 0x4228 then 0x4269, to 24:1, one connection" \
    '[ "$rc" = 0 ] && [ "$(connects)" = 1 ] && [ "$(sysex)" = 2 ] &&
     [ "$(grep "^sysex " "$MIDI" | tr "\n" ";")" = "sysex $MSG;sysex $MSG10;" ] &&
     [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq list;seq connect 24:1;sysex $MSG;sysex $MSG10;seq close;" ]'
ok "restore-stereo: both transfers logged, close only after both and the settle time" \
    'grep -q "message 1 of 2 passed to ALSA" "$T/out.txt" && grep -q "message 2 of 2 passed to ALSA" "$T/out.txt" &&
     [ "$(grep -n "both messages passed to ALSA; waiting 200 ms" "$T/out.txt" | cut -d: -f1)" -lt "$(grep -n "\] closed$" "$T/out.txt" | cut -d: -f1)" ] &&
     grep -q "Type SEND to send these TWO messages" "$T/out.txt"'
ok "restore-stereo: no other register written (no crossed routing, no volume)" \
    '! grep "^sysex " "$MIDI" | grep -vxE "sysex ($MSG|$MSG10)" | grep -q . &&
     ! grep -qE "sysex .*(00 00 24 11 04|00 00 20 13 04|30 02|30 0A)" "$MIDI"'

for answer in "" "send" "SEND SEND" "y"; do
    reset 20
    rc=$(b3 "$answer" --restore-stereo)
    ok "restore-stereo confirmation '$answer': aborted, no connect, nothing sent" \
        '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && grep -q "aborted: nothing sent" "$T/out.txt"'
done

reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=2 b3 SEND --restore-stereo)
ok "restore-stereo: second message fails: exit 4, first passed, reported as 1 of 2, no retry" \
    '[ "$rc" = 4 ] && [ "$(grep -c "^sysex F0" "$MIDI")" = 1 ] && grep -qx "sysex $MSG" "$MIDI" &&
     [ "$(grep -c "^sysex FAILED" "$MIDI")" = 1 ] && grep -q "message 2 of 2 failed .*1 of 2 passed to ALSA" "$T/out.txt" &&
     grep -q "nothing retried" "$T/out.txt" && grep -q "seq close" "$MIDI"'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 b3 SEND --restore-stereo)
ok "restore-stereo: first message fails: exit 4, the second is not sent" \
    '[ "$rc" = 4 ] && [ "$(grep -c "^sysex" "$MIDI")" = 1 ] && grep -q "0 of 2 passed to ALSA" "$T/out.txt"'
reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 b3 SEND --restore-stereo)
ok "restore-stereo without write functions: refused before asking" \
    '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && ! grep -q "Type SEND" "$T/out.txt"'
reset 20; dev 3-2.4.1.1.4 0424 3fdd 54; card 3-2.4.1.1.4 2
rc=$(b3 SEND --restore-stereo)
ok "restore-stereo with two units in CC mode: refused, nothing sent" \
    '[ "$rc" = 3 ] && [ ! -e "$MIDI" ]'
reset 20; dev "$PORT" 2a39 3fcd 53
rc=$(b3 SEND --restore-stereo)
ok "restore-stereo with the unit in proprietary mode: refused, nothing sent" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ]'
reset 20; seqports 20 "Fireface 802 (24047128) Port 1"
rc=$(b3 SEND --restore-stereo)
ok "restore-stereo with a wrong port name: refused, nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
for a in "--restore-stereo --send" "--send --restore-stereo" "--restore-stereo --restore-stereo" \
         "--restore-stereo 9" "--restore-stereo --connection"; do
    reset 20
    # shellcheck disable=SC2086
    rc=$(b3 SEND $a)
    ok "arguments '$a': usage error, nothing opened" '[ "$rc" = 2 ] && [ ! -e "$MIDI" ]'
done

# --- refusals before anything is sent ----------------------------------------------
refused() { # refused <description>: exit 3, no connect, nothing sent
    rc=$(b3 SEND --send)
    ok "$1: refused, nothing sent" '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ]'
}
reset 20; rm -rf -- "${DEVS:?}/$PORT"; refused "no Fireface 802"
reset 20; dev "$PORT" 2a39 3fcd 53; refused "unit in proprietary mode"
reset 20; dev 3-2.4.1.1.4 0424 3fdd 54; card 3-2.4.1.1.4 2; refused "two units in CC mode without --connection"
reset 20; rm -rf -- "${DEVS:?}/$PORT/$PORT:1.0"; refused "no sound card"
reset 20; rm -f -- "$R/seq-ports"; refused "no sequencer port"
reset 20; seqports 20 "Fireface 802 (24047128) Port 1"; refused "port 1 not named 'Port 2'"
reset 20; printf '20 1 kernel 1 w a Port 2\n21 1 kernel 1 w b Port 2\n' > "$R/seq-ports"; refused "two kernel clients of the card"
reset 20; rc=$(FF802_SIM_SEQ_OPEN_ERR=1 b3 SEND --send)
ok "sequencer cannot be opened: refused" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
reset 20; rc=$(b3 SEND --send --connection 9-9)
ok "--connection to a missing device: refused" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ]'

# --- usage ----------------------------------------------------------------------------
reset 20
for a in "" "--send test" "--send restore" "--send 0x4228" "--send --send" "--dry-run --dry-run" \
         "--dry-run --send" "--write 4228 9000" "--dry-run extra" "--send --connection"; do
    # shellcheck disable=SC2086
    rc=$(b3 SEND $a)
    ok "arguments '$a': usage error, nothing opened" '[ "$rc" = 2 ] && [ ! -e "$MIDI" ]'
done
env -u FF802_AUTO_ROOT "$SIM" --dry-run > "$T/noroot.txt" 2>&1; rc=$?
ok "simulation refuses to run without its private root" '[ "$rc" = 2 ]'

# --- source: exactly the one message --------------------------------------------------
B3SRC=$SRC/tools/hw-test/ff802_mixer_b3.c
ok "source: exactly three SysEx literals (route, stereo-9, stereo-10), registers 0x4228/0x4269, value 0x9000" \
    '[ "$(grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00," "$B3SRC")" = 3 ] &&
     [ "$(grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x22, 0x11, 0x0C, 0xF7 }" "$B3SRC")" = 2 ] &&
     [ "$(grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x26, 0x13, 0x0C, 0xF7 }" "$B3SRC")" = 1 ] &&
     [ "$(grep -c "^#define B3_REG 0x4228u$" "$B3SRC")" = 1 ] &&
     [ "$(grep -c "^#define B3_REG_10 0x4269u$" "$B3SRC")" = 1 ] &&
     [ "$(grep -oE "\"(route|stereo-9|stereo-10)\", 0x[0-9A-F]{4}u" "$B3SRC" | tr "\n" ";")" = "\"route\", 0x9000u;\"stereo-9\", 0x9000u;\"stereo-10\", 0x9000u;" ] &&
     [ "$(grep -oE "0x4[0-9A-F]{3}u" "$B3SRC" | sort -u | tr "\n" " ")" = "0x4228u 0x4269u " ] &&
     ! grep -vE "^ ?(\*|/\*)" "$B3SRC" | grep -Eq "0x0000u|0xFF38u|0x2600u|0x4229|0x4268"'
ok "source: no other message source (no mixer table, no free address or value)" \
    '! grep -Eq "mixer_init|ff802_rme_sysex_reg|strtoul|strtol|sscanf|atoi" "$B3SRC"'
ok "simulation build links neither libasound nor libusb" \
    '! "$NM" -D --undefined-only "$SIM" | grep -Eq " (snd_|libusb_)"'

# --- the real program (only if built) ----------------------------------------------
if [ "$REAL" != "-" ]; then
    "$REAL" --version > "$T/rv.txt" 2>&1; rc1=$?
    "$REAL" --help > "$T/rh.txt" 2>&1; rc2=$?
    "$REAL" --send test > /dev/null 2>&1; rc3=$?
    ok "real program: --version/--help, usage error before any device access" \
        '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$rc3" = 2 ] &&
         grep -Eq "^ff802-mixer-b3 [0-9.]+ \(ALSA sequencer, mixer writes enabled\)$" "$T/rv.txt"'
    und=$("$NM" -D --undefined-only "$REAL" | awk '{ print $2 }' | sed 's/@.*//')
    defs=$("$NM" "$REAL" | awk '{ print $3 }')
    has() { printf '%s\n' "$und" | grep -qx "$1"; }
    none_of() { for s in "$@"; do has "$s" && return 1; done; return 0; }
    ok "real program: sequencer subscribe and direct output linked (the write path of B.3)" \
        'has snd_seq_open && has snd_seq_connect_to && has snd_seq_event_output_direct'
    ok "real program: no rawmidi, control writes, PCM, libusb" \
        'none_of snd_rawmidi_open snd_rawmidi_write snd_ctl_open snd_ctl_elem_write snd_pcm_open \
                 libusb_init libusb_open libusb_bulk_transfer'
    ok "real program: does not contain the 24-register table or the initialisation flow" \
        '! printf "%s\n" "$defs" | grep -Eqx "ff802_mixer_init_table|ff802_mixer_run|ff802_rme_sysex_reg|ff802_auto_handle"'
else
    echo "skip real program checks (FF802_BUILD_MIXER_B3=OFF)"
fi

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
