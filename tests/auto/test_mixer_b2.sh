#!/bin/sh
# Hardware test program B.2 (ff802-mixer-b2), without hardware:
#  - the simulation build (simulated MIDI backend, fake sysfs below a
#    private root): detection, port by card number with changing client
#    numbers, the two messages, confirmation, every refusal;
#  - the real program, if built (FF802_BUILD_MIXER_B2=ON): only --help,
#    --version and usage errors (nothing that reaches sysfs or ALSA), and
#    its linked symbols with nm;
#  - the source: exactly the two messages.
#
# Usage: test_mixer_b2.sh <source dir> <sim program> <real program or -> <nm> <scratch dir>

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
    */b2-scratch) ;;
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

TEST_MSG="F0 00 20 0D 10 00 38 7E 03 30 0A F7"
RESTORE_MSG="F0 00 20 0D 10 00 00 00 00 30 02 F7"

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
reset() { # the host of B.1: one unit in CC mode, card 1, client <n>
    rm -rf -- "${R:?}"
    mkdir -p "$DEVS"
    dev "$PORT" 0424 3fdd 53; card "$PORT" 1; seqports "${1:-20}"
}
b2() { # b2 <stdin text> <args...>: print the exit code
    input=$1; shift
    printf '%s\n' "$input" | env FF802_AUTO_ROOT="$R" "$SIM" "$@" > "$T/out.txt" 2>&1
    echo $?
}
sysex() { n=$(grep -c '^sysex ' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
connects() { n=$(grep -c '^seq connect' "$MIDI" 2>/dev/null); echo "${n:-0}"; }

# --- dry run ------------------------------------------------------------------------
reset 20
rc=$(b2 "" --dry-run)
ok "dry run (B.1 host): unit, card 1, port 20:1 selected, nothing sent" \
    '[ "$rc" = 0 ] && grep -q "^identity: Fireface 802, class-compliant mode" "$T/out.txt" &&
     grep -q "^sound card (sysfs): found, card 1" "$T/out.txt" &&
     grep -q "^selected port: 20:1 .Fireface 802 (24047128) Port 2.$" "$T/out.txt" &&
     grep -q "^dry run: nothing sent" "$T/out.txt" &&
     [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq close;" ]'
ok "dry run lists exactly the two messages of register 0x2600" \
    '[ "$(grep -c "^message " "$T/out.txt")" = 2 ] &&
     grep -q "^message test    0x2600 = 0xFF38  Hardware Output 9 (Phones 9) volume -20.0 dB$" "$T/out.txt" &&
     grep -q "^message restore 0x2600 = 0x0000  Hardware Output 9 (Phones 9) volume 0.0 dB$" "$T/out.txt" &&
     grep -q "^                $TEST_MSG$" "$T/out.txt" && grep -q "^                $RESTORE_MSG$" "$T/out.txt"'
for c in 24 128 7; do
    reset "$c"
    rc=$(b2 "" --dry-run)
    ok "changing sequencer client number $c: found by card number" \
        '[ "$rc" = 0 ] && grep -q "^selected port: $c:1 " "$T/out.txt"'
done
reset 20
rc=$(b2 "" --dry-run --connection "$PORT")
ok "dry run with --connection" '[ "$rc" = 0 ] && grep -q "^selected port: 20:1 " "$T/out.txt"'

# --- send ----------------------------------------------------------------------------
reset 24
rc=$(b2 SEND --send test)
ok "send test: confirmed, connect to 24:1, exactly ONE message, the test bytes" \
    '[ "$rc" = 0 ] && [ "$(connects)" = 1 ] && grep -qx "seq connect 24:1" "$MIDI" &&
     [ "$(sysex)" = 1 ] && grep -qx "sysex $TEST_MSG" "$MIDI" &&
     [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq list;seq connect 24:1;sysex $TEST_MSG;seq close;" ] &&
     grep -q "sent 12 bytes" "$T/out.txt"'
reset 24
rc=$(b2 SEND --send restore)
ok "send restore: exactly ONE message, the restore bytes" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 1 ] && grep -qx "sysex $RESTORE_MSG" "$MIDI"'

for answer in "" "send" "yes" "SEND " "SENDX"; do
    reset 20
    rc=$(b2 "$answer" --send test)
    ok "confirmation '$answer': aborted, no connect, nothing sent" \
        '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && grep -q "aborted: nothing sent" "$T/out.txt"'
done

reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 b2 SEND --send test)
ok "program without write functions: refused before asking, nothing sent" \
    '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && grep -q "not compiled into this program" "$T/out.txt" &&
     ! grep -q "Type SEND" "$T/out.txt"'
reset 20
rc=$(FF802_SIM_SEQ_CONNECT_ERR=busy b2 SEND --send test)
ok "connect fails (busy): exit 3, nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 b2 SEND --send test)
ok "sending fails: exit 4, one attempt, no retry" \
    '[ "$rc" = 4 ] && [ "$(grep -c "^sysex" "$MIDI")" = 1 ] && grep -q "nothing retried" "$T/out.txt"'

# --- refusals before anything is sent ----------------------------------------------
refused() { # refused <description>: exit 3, no connect, nothing sent
    rc=$(b2 SEND --send test)
    ok "$1: refused, nothing sent" '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ]'
}
reset 20; rm -rf -- "${DEVS:?}/$PORT"; refused "no Fireface 802"
reset 20; dev "$PORT" 2a39 3fcd 53; refused "unit in proprietary mode"
reset 20; dev 3-2.4.1.1.4 0424 3fdd 54; card 3-2.4.1.1.4 2; refused "two units in CC mode without --connection"
reset 20; rm -rf -- "${DEVS:?}/$PORT/$PORT:1.0"; refused "no sound card"
reset 20; rm -f -- "$R/seq-ports"; refused "no sequencer port"
reset 20; seqports 20 "Fireface 802 (24047128) Port 1"; refused "port 1 not named 'Port 2'"
reset 20; printf '20 1 kernel 1 w a Port 2\n21 1 kernel 1 w b Port 2\n' > "$R/seq-ports"; refused "two kernel clients of the card"
reset 20; rc=$(FF802_SIM_SEQ_OPEN_ERR=1 b2 SEND --send test)
ok "sequencer cannot be opened: refused" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
reset 20; rc=$(b2 SEND --send test --connection 9-9)
ok "--connection to a missing device: refused" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ]'

# --- usage ----------------------------------------------------------------------------
reset 20
for a in "" "--send" "--send all" "--send 0x2600" "--send test --send restore" "--dry-run --dry-run" \
         "--write 2600 ff38" "--dry-run extra" "--send test --connection"; do
    # shellcheck disable=SC2086
    rc=$(b2 SEND $a)
    ok "arguments '$a': usage error, nothing opened" '[ "$rc" = 2 ] && [ ! -e "$MIDI" ]'
done
env -u FF802_AUTO_ROOT "$SIM" --dry-run > "$T/noroot.txt" 2>&1; rc=$?
ok "simulation refuses to run without its private root" '[ "$rc" = 2 ]'

# --- source: exactly the two messages ------------------------------------------------
B2SRC=$SRC/tools/hw-test/ff802_mixer_b2.c
ok "source: exactly two SysEx literals, both for register 0x2600, values 0xFF38 and 0x0000" \
    '[ "$(grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00," "$B2SRC")" = 2 ] &&
     grep -q "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x38, 0x7E, 0x03, 0x30, 0x0A, 0xF7 }" "$B2SRC" &&
     grep -q "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x00, 0x30, 0x02, 0xF7 }" "$B2SRC" &&
     [ "$(grep -c "^#define B2_REG 0x2600u$" "$B2SRC")" = 1 ] &&
     [ "$(grep -oE "\{ \"[a-z]+\", 0x[0-9A-F]{4}u" "$B2SRC" | tr "\n" ";")" = "{ \"test\", 0xFF38u;{ \"restore\", 0x0000u;" ]'
ok "source: no other message source (no mixer table, no free address or value)" \
    '! grep -Eq "mixer_init|ff802_rme_sysex_reg|strtoul|strtol|sscanf|atoi" "$B2SRC"'

ok "simulation build links neither libasound nor libusb" \
    '! "$NM" -D --undefined-only "$SIM" | grep -Eq " (snd_|libusb_)"'

# --- the real program (only if built) ----------------------------------------------
if [ "$REAL" != "-" ]; then
    "$REAL" --version > "$T/rv.txt" 2>&1; rc1=$?
    "$REAL" --help > "$T/rh.txt" 2>&1; rc2=$?
    "$REAL" --send everything > /dev/null 2>&1; rc3=$?
    ok "real program: --version/--help, usage error before any device access" \
        '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$rc3" = 2 ] &&
         grep -Eq "^ff802-mixer-b2 [0-9.]+ \(ALSA sequencer, mixer writes enabled\)$" "$T/rv.txt"'
    und=$("$NM" -D --undefined-only "$REAL" | awk '{ print $2 }' | sed 's/@.*//')
    defs=$("$NM" "$REAL" | awk '{ print $3 }')
    has() { printf '%s\n' "$und" | grep -qx "$1"; }
    none_of() { for s in "$@"; do has "$s" && return 1; done; return 0; }
    ok "real program: sequencer subscribe and direct output linked (the write path of B.2)" \
        'has snd_seq_open && has snd_seq_connect_to && has snd_seq_event_output_direct'
    ok "real program: no rawmidi, control writes, PCM, libusb" \
        'none_of snd_rawmidi_open snd_rawmidi_write snd_ctl_open snd_ctl_elem_write snd_pcm_open \
                 libusb_init libusb_open libusb_bulk_transfer'
    ok "real program: does not contain the 24-register table or the initialisation flow" \
        '! printf "%s\n" "$defs" | grep -Eqx "ff802_mixer_init_table|ff802_mixer_run|ff802_rme_sysex_reg|ff802_auto_handle"'
else
    echo "skip real program checks (FF802_BUILD_MIXER_B2=OFF)"
fi

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
