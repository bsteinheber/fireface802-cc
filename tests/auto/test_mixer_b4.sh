#!/bin/sh
# Hardware test program B.4 (ff802-mixer-b4), without hardware:
#  - the simulation build (simulated MIDI backend, fake sysfs below a
#    private root): dry run by default, the one message 0x4261 = 0x0000,
#    confirmation, every refusal;
#  - the real program, if built (FF802_BUILD_MIXER_B4=ON): only --help,
#    --version and usage errors (nothing that reaches sysfs or ALSA), and
#    its linked symbols with nm;
#  - the source: exactly the one message.
#
# Usage: test_mixer_b4.sh <source dir> <sim program> <real program or -> <nm> <scratch dir>

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
    */b4-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
R=$T/root
PORT=3-2.4.1.1.3
DEVS=$R/sys/bus/usb/devices
MIDI=$R/midi.log
unset FF802_SIM_SEQ_OPEN_ERR FF802_SIM_SEQ_LIST_ERR FF802_SIM_SEQ_CONNECT_ERR \
      FF802_SIM_SEQ_SEND_ERR_AT FF802_SIM_MIXER_WRITE FF802_SIM_MIXER_TIMEOUT_MS \
      FF802_SIM_SEQ_PORTS_AFTER

# 0x4261 = 0x0000, computed here with an independent shell implementation
sx() { a=$1; v=$2; w=$(( (a << 16) | v )); p=0; x=$w
       while [ "$x" -gt 0 ]; do p=$((p ^ (x & 1))); x=$((x >> 1)); done
       [ "$p" = 0 ] && w=$(( w | (1 << 31) ))
       printf 'F0 00 20 0D 10 00'
       for i in 0 1 2 3 4; do printf ' %02X' $(( (w >> (7 * i)) & 0x7F )); done
       printf ' F7\n'; }
MSG=$(sx $((0x4261)) 0)
REG=$(( 0x4000 | (9 << 6) | (0x20 + 1) ))     # output index 9 (Phones 10), Playback 2

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
reset() {
    rm -rf -- "${R:?}"
    mkdir -p "$DEVS"
    dev "$PORT" 0424 3fdd 53; card "$PORT" 1; seqports "${1:-20}"
}
b4() { # b4 <stdin text> <args...>: print the exit code
    input=$1; shift
    printf '%s\n' "$input" | env FF802_AUTO_ROOT="$R" "$SIM" "$@" > "$T/out.txt" 2>&1
    echo $?
}
sysex() { n=$(grep -c '^sysex ' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
connects() { n=$(grep -c '^seq connect' "$MIDI" 2>/dev/null); echo "${n:-0}"; }

# --- register and encoding --------------------------------------------------------
ok "register: Playback 2 -> Output 10 by the documented formula is 0x4261" '[ "$REG" = $((0x4261)) ]'
ok "SysEx of 0x4261 = 0x0000 (independent shell computation, odd parity, parity bit 0)" \
    '[ "$MSG" = "F0 00 20 0D 10 00 00 00 04 13 04 F7" ]'

# --- dry run (default) --------------------------------------------------------------
for a in "" "--dry-run"; do
    reset 20
    # shellcheck disable=SC2086
    rc=$(b4 SEND $a)
    ok "dry run '${a:-<no option>}': unit, card 1, port 20:1, message shown, nothing connected or sent" \
        '[ "$rc" = 0 ] && grep -q "^ff802-mixer-b4 [0-9.]*: dry run$" "$T/out.txt" &&
         grep -q "^identity: Fireface 802, class-compliant mode" "$T/out.txt" &&
         grep -q "^sound card (sysfs): found, card 1" "$T/out.txt" &&
         grep -q "^selected port: 20:1 .Fireface 802 (24047128) Port 2.$" "$T/out.txt" &&
         grep -q "^message: register 0x4261 = 0x0000  Playback 2 -> Hardware Output 10 (Phones 10) off$" "$T/out.txt" &&
         grep -q "^         word 0x42610000 (odd parity), SysEx $MSG$" "$T/out.txt" &&
         grep -q "^note: no restore" "$T/out.txt" && grep -q "^dry run: nothing sent" "$T/out.txt" &&
         ! grep -q "Type SEND" "$T/out.txt" &&
         [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq close;" ]'
done
for c in 24 128; do
    reset "$c"
    rc=$(b4 "")
    ok "changing sequencer client number $c: found by card number" \
        '[ "$rc" = 0 ] && grep -q "^selected port: $c:1 " "$T/out.txt"'
done

# --- send ---------------------------------------------------------------------------
reset 24
rc=$(b4 SEND --send)
ok "send: shows identity, mode, port, register, value, SysEx; then exactly ONE message 0x4261 = 0x0000" \
    '[ "$rc" = 0 ] && grep -q "^About to send to 24:1 ($PORT, Fireface 802, class-compliant mode, card 1):" "$T/out.txt" &&
     grep -q "^  register 0x4261 = 0x0000  Playback 2 -> Hardware Output 10 (Phones 10) off$" "$T/out.txt" &&
     grep -q "^  SysEx $MSG$" "$T/out.txt" && grep -q "cannot be restored" "$T/out.txt" &&
     [ "$(connects)" = 1 ] && [ "$(sysex)" = 1 ] && grep -qx "sysex $MSG" "$MIDI" &&
     [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq list;seq connect 24:1;sysex $MSG;seq close;" ] &&
     grep -q "waiting 200 ms before closing" "$T/out.txt"'

for answer in "" "send" "yes" "SEND " "SENDX"; do
    reset 20
    rc=$(b4 "$answer" --send)
    ok "confirmation '$answer': aborted, no connect, nothing sent" \
        '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && grep -q "aborted: nothing sent" "$T/out.txt"'
done
reset 20
rc=$(env FF802_AUTO_ROOT="$R" "$SIM" --send < /dev/null > "$T/out.txt" 2>&1; echo $?)
ok "no confirmation at all (end of input): aborted, nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'

reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 b4 SEND --send)
ok "program without write functions: refused before asking, nothing sent" \
    '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && ! grep -q "Type SEND" "$T/out.txt"'
reset 20
rc=$(FF802_SIM_SEQ_CONNECT_ERR=busy b4 SEND --send)
ok "connect fails (busy): exit 3, nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 b4 SEND --send)
ok "sending fails: exit 4, one attempt, no retry" \
    '[ "$rc" = 4 ] && [ "$(grep -c "^sysex" "$MIDI")" = 1 ] && grep -q "nothing retried" "$T/out.txt"'

# --- refusals: wrong device or mode ------------------------------------------------
refused() { # refused <description>: exit 3, no connect, nothing sent
    rc=$(b4 SEND --send)
    ok "$1: refused, nothing sent" '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ]'
}
reset 20; rm -rf -- "${DEVS:?}/$PORT"; refused "no Fireface 802"
reset 20; dev "$PORT" 2a39 3fcd 53; refused "unit in proprietary mode"
reset 20; echo "Fireface 802 FS (1)" > "$DEVS/$PORT/product"; refused "Fireface 802 FS"
reset 20; echo Microchip > "$DEVS/$PORT/manufacturer"; refused "wrong manufacturer"
reset 20; echo 12 > "$DEVS/$PORT/speed"; refused "not high speed"
reset 20; dev 3-2.4.1.1.4 0424 3fdd 54; card 3-2.4.1.1.4 2; refused "two units in CC mode without --connection"
reset 20; rm -rf -- "${DEVS:?}/$PORT/$PORT:1.0"; refused "no sound card"
reset 20; rm -f -- "$R/seq-ports"; refused "no sequencer port"
reset 20; seqports 20 "Fireface 802 (24047128) Port 1"; refused "port 1 not named 'Port 2'"
reset 20; rc=$(b4 SEND --send --connection "$PORT")
ok "--connection to the unit: sends the one message" '[ "$rc" = 0 ] && [ "$(sysex)" = 1 ]'
reset 20; dev "$PORT" 2a39 3fcd 53; rc=$(b4 SEND --send --connection "$PORT")
ok "--connection to a unit in proprietary mode: refused" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ]'

# --- no other register can be written --------------------------------------------------
reset 20
for a in "--send --send" "--send --dry-run" "--dry-run --send" "--send 0x4069" "--register 0x4069" \
         "--value 0x9000" "--send test" "--restore-stereo" "--connection" "extra"; do
    # shellcheck disable=SC2086
    rc=$(b4 SEND $a)
    ok "arguments '$a': usage error, nothing opened" '[ "$rc" = 2 ] && [ ! -e "$MIDI" ]'
done
env -u FF802_AUTO_ROOT "$SIM" > /dev/null 2>&1; rc=$?
ok "simulation refuses to run without its private root" '[ "$rc" = 2 ]'

B4SRC=$SRC/tools/hw-test/ff802_mixer_b4.c
code() { grep -vE "^ ?(\*|/\*)" "$B4SRC"; }
ok "source: exactly one SysEx literal, the one for 0x4261 = 0x0000" \
    '[ "$(code | grep -cE "\{ 0xF0(, 0x[0-9A-F]{2}){11} \}")" = 1 ] &&
     [ "$(code | grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x04, 0x13, 0x04, 0xF7 }")" = 1 ] &&
     [ "$(code | grep -oE "0x4[0-9A-Fa-f]{3}u" | sort -u)" = "0x4261u" ] &&
     [ "$(code | grep -c "^#define B4_REG     0x4261u$")" = 1 ] &&
     [ "$(code | grep -c "^#define B4_VALUE   0x0000u$")" = 1 ]'
ok "source: no other message source (no mixer table, no free address or value, no restore)" \
    '! code | grep -Eq "mixer_init|ff802_rme_sysex_reg|strtoul|strtol|sscanf|atoi|0x9000|0x4069|restore-"'
ok "simulation build links neither libasound nor libusb" \
    '! "$NM" -D --undefined-only "$SIM" | grep -Eq " (snd_|libusb_)"'

# --- the real program (only if built) ----------------------------------------------
if [ "$REAL" != "-" ]; then
    "$REAL" --version > "$T/rv.txt" 2>&1; rc1=$?
    "$REAL" --help > /dev/null 2>&1; rc2=$?
    "$REAL" --send --send > /dev/null 2>&1; rc3=$?
    ok "real program: --version/--help, usage error before any device access" \
        '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$rc3" = 2 ] &&
         grep -Eq "^ff802-mixer-b4 [0-9.]+ \(ALSA sequencer, mixer writes enabled\)$" "$T/rv.txt"'
    und=$("$NM" -D --undefined-only "$REAL" | awk '{ print $2 }' | sed 's/@.*//')
    defs=$("$NM" "$REAL" | awk '{ print $3 }')
    has() { printf '%s\n' "$und" | grep -qx "$1"; }
    none_of() { for s in "$@"; do has "$s" && return 1; done; return 0; }
    ok "real program: sequencer subscribe and direct output linked (the write path of B.4)" \
        'has snd_seq_open && has snd_seq_connect_to && has snd_seq_event_output_direct'
    ok "real program: no rawmidi, control writes, PCM, libusb" \
        'none_of snd_rawmidi_open snd_rawmidi_write snd_ctl_open snd_ctl_elem_write snd_pcm_open \
                 libusb_init libusb_open libusb_bulk_transfer'
    ok "real program: does not contain the 24-register table or the initialisation flow" \
        '! printf "%s\n" "$defs" | grep -Eqx "ff802_mixer_init_table|ff802_mixer_run|ff802_rme_sysex_reg|ff802_auto_handle"'
else
    echo "skip real program checks (FF802_BUILD_MIXER_B4=OFF)"
fi

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
