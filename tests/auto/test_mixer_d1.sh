#!/bin/sh
# Diagnosis D1 (ff802-mixer-d1), without hardware:
#  - the simulation build (simulated MIDI backend, fake sysfs below a
#    private root): dry run by default, --apply (0x4269 = 0x0000) and
#    --restore (0x4269 = 0x9000) each exactly one message, confirmation,
#    every refusal;
#  - the real program, if built (FF802_BUILD_MIXER_D1=ON): only --help,
#    --version and usage errors (nothing that reaches sysfs or ALSA), and
#    its linked symbols with nm;
#  - the source: exactly the two messages for 0x4269.
#
# Usage: test_mixer_d1.sh <source dir> <sim program> <real program or -> <nm> <scratch dir>

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
    */d1-scratch) ;;
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

# The two messages, computed here with an independent shell implementation
sx() { a=$1; v=$2; w=$(( (a << 16) | v )); p=0; x=$w
       while [ "$x" -gt 0 ]; do p=$((p ^ (x & 1))); x=$((x >> 1)); done
       [ "$p" = 0 ] && w=$(( w | (1 << 31) ))
       printf 'F0 00 20 0D 10 00'
       for i in 0 1 2 3 4; do printf ' %02X' $(( (w >> (7 * i)) & 0x7F )); done
       printf ' F7\n'; }
OFF=$(sx $((0x4269)) 0)
ON=$(sx $((0x4269)) $((0x9000)))
WRONG="F0 00 20 0D 10 00 00 00 24 13 04 F7"     # wrong parity, given in an earlier report
REG=$(( 0x4000 | (9 << 6) | (0x20 + 9) ))       # output index 9 (Phones 10), Playback 10

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
d1() { # d1 <stdin text> <args...>: print the exit code
    input=$1; shift
    printf '%s\n' "$input" | env FF802_AUTO_ROOT="$R" "$SIM" "$@" > "$T/out.txt" 2>&1
    echo $?
}
sysex() { n=$(grep -c '^sysex ' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
connects() { n=$(grep -c '^seq connect' "$MIDI" 2>/dev/null); echo "${n:-0}"; }

# --- register and encoding --------------------------------------------------------
ok "register: Playback 10 -> Output 10 by the documented formula is 0x4269" '[ "$REG" = $((0x4269)) ]'
ok "SysEx 0x4269 = 0x0000: parity bit set (six address bits), last septet 0C" \
    '[ "$OFF" = "F0 00 20 0D 10 00 00 00 24 13 0C F7" ] && [ "$OFF" != "$WRONG" ]'
ok "SysEx 0x4269 = 0x9000: entry 10 of the mixer initialisation" \
    '[ "$ON" = "F0 00 20 0D 10 00 00 20 26 13 0C F7" ]'

# --- dry run (default) --------------------------------------------------------------
for a in "" "--dry-run"; do
    reset 20
    # shellcheck disable=SC2086
    rc=$(d1 SEND $a)
    ok "dry run '${a:-<no option>}': unit, card 1, port 20:1, both messages shown, nothing connected or sent" \
        '[ "$rc" = 0 ] && grep -q "^ff802-mixer-d1 [0-9.]*: dry run$" "$T/out.txt" &&
         grep -q "^identity: Fireface 802, class-compliant mode" "$T/out.txt" &&
         grep -q "^sound card (sysfs): found, card 1" "$T/out.txt" &&
         grep -q "^selected port: 20:1 .Fireface 802 (24047128) Port 2.$" "$T/out.txt" &&
         grep -q "^message --apply   register 0x4269 = 0x0000  Playback 10 -> Hardware Output 10 (Phones 10) off (test)$" "$T/out.txt" &&
         grep -q "^                  word 0xC2690000 (odd parity), SysEx $OFF$" "$T/out.txt" &&
         grep -q "^message --restore register 0x4269 = 0x9000  Playback 10 -> Hardware Output 10 (Phones 10) 0 dB$" "$T/out.txt" &&
         grep -q "^                  word 0xC2699000 (odd parity), SysEx $ON$" "$T/out.txt" &&
         grep -q "^dry run: nothing sent" "$T/out.txt" && ! grep -q "Type SEND" "$T/out.txt" &&
         [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq close;" ]'
done
reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 d1 "")
ok "dry run also in a build without writes" '[ "$rc" = 0 ] && [ "$(sysex)" = 0 ]'
for c in 24 128; do
    reset "$c"
    rc=$(d1 "")
    ok "changing sequencer client number $c: found by card number" \
        '[ "$rc" = 0 ] && grep -q "^selected port: $c:1 " "$T/out.txt"'
done

# --- apply / restore ---------------------------------------------------------------
reset 24
rc=$(d1 SEND --apply)
ok "--apply: shows everything, then exactly ONE message 0x4269 = 0x0000" \
    '[ "$rc" = 0 ] && grep -q "^ff802-mixer-d1 [0-9.]*: apply$" "$T/out.txt" &&
     grep -q "^About to send to 24:1 ($PORT, Fireface 802, class-compliant mode, card 1):" "$T/out.txt" &&
     grep -q "^  register 0x4269 = 0x0000  Playback 10 -> Hardware Output 10 (Phones 10) off (test)$" "$T/out.txt" &&
     grep -q "^  word 0xC2690000, SysEx $OFF$" "$T/out.txt" &&
     [ "$(connects)" = 1 ] && [ "$(sysex)" = 1 ] && grep -qx "sysex $OFF" "$MIDI" && ! grep -q "$WRONG" "$MIDI" &&
     [ "$(tr "\n" ";" < "$MIDI")" = "seq open;seq list;seq list;seq connect 24:1;sysex $OFF;seq close;" ] &&
     grep -q "waiting 200 ms before closing" "$T/out.txt"'
reset 24
rc=$(d1 SEND --restore)
ok "--restore: exactly ONE message 0x4269 = 0x9000" \
    '[ "$rc" = 0 ] && grep -q "^ff802-mixer-d1 [0-9.]*: restore$" "$T/out.txt" &&
     grep -q "^  register 0x4269 = 0x9000  Playback 10 -> Hardware Output 10 (Phones 10) 0 dB$" "$T/out.txt" &&
     [ "$(connects)" = 1 ] && [ "$(sysex)" = 1 ] && grep -qx "sysex $ON" "$MIDI"'
reset 20
rc=$(d1 SEND --apply)
ok "--apply never restores by itself (no second message)" '[ "$rc" = 0 ] && [ "$(sysex)" = 1 ] && ! grep -q "sysex $ON" "$MIDI"'

for mode in --apply --restore; do
    for answer in "" "send" "yes" "SEND " "SENDX"; do
        reset 20
        rc=$(d1 "$answer" $mode)
        ok "$mode confirmation '$answer': aborted, no connect, nothing sent" \
            '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && grep -q "aborted: nothing sent" "$T/out.txt"'
    done
    reset 20
    rc=$(env FF802_AUTO_ROOT="$R" "$SIM" $mode < /dev/null > "$T/out.txt" 2>&1; echo $?)
    ok "$mode without any confirmation (end of input): aborted" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
    reset 20
    rc=$(FF802_SIM_MIXER_WRITE=0 d1 SEND $mode)
    ok "$mode in a program without write functions: refused before asking" \
        '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && ! grep -q "Type SEND" "$T/out.txt"'
done

# --- MIDI errors --------------------------------------------------------------------
reset 20
rc=$(FF802_SIM_SEQ_OPEN_ERR=1 d1 SEND --apply)
ok "sequencer cannot be opened: reported, exit 3, nothing sent" \
    '[ "$rc" = 3 ] && grep -q "cannot open the ALSA sequencer" "$T/out.txt" && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_SEQ_LIST_ERR=1 d1 SEND --apply)
ok "port list fails: reported, exit 3" '[ "$rc" = 3 ] && grep -q "cannot list the sequencer ports" "$T/out.txt"'
reset 20
rc=$(FF802_SIM_SEQ_CONNECT_ERR=busy d1 SEND --apply)
ok "connect fails (busy): reported, exit 3, nothing sent" \
    '[ "$rc" = 3 ] && grep -q "cannot connect to 20:1" "$T/out.txt" && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 d1 SEND --restore)
ok "sending fails: exit 4, one attempt, no retry" \
    '[ "$rc" = 4 ] && [ "$(grep -c "^sysex" "$MIDI")" = 1 ] && grep -q "nothing retried" "$T/out.txt"'

# --- refusals: wrong device or mode ------------------------------------------------
refused() { # refused <description>: exit 3, no connect, nothing sent
    rc=$(d1 SEND --apply)
    ok "$1: refused, nothing sent" '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ]'
}
reset 20; rm -rf -- "${DEVS:?}/$PORT"; refused "no Fireface 802"
reset 20; dev "$PORT" 2a39 3fcd 53; refused "unit in proprietary mode"
reset 20; echo "Fireface 802 FS (1)" > "$DEVS/$PORT/product"; refused "Fireface 802 FS"
reset 20; dev 3-2.4.1.1.4 0424 3fdd 54; card 3-2.4.1.1.4 2; refused "two units in CC mode without --connection"
reset 20; rm -rf -- "${DEVS:?}/$PORT/$PORT:1.0"; refused "no sound card"
reset 20; rm -f -- "$R/seq-ports"; refused "no sequencer port"
reset 20; seqports 20 "Fireface 802 (24047128) Port 1"; refused "port 1 not named 'Port 2'"

# --- no other register can be written ----------------------------------------------
reset 20
for a in "--apply --restore" "--restore --apply" "--apply --apply" "--dry-run --apply" \
         "--apply 0x0000" "--register 0x4261" "--value 0x9000" "--send" "--connection" "extra"; do
    # shellcheck disable=SC2086
    rc=$(d1 SEND $a)
    ok "arguments '$a': usage error, nothing opened" '[ "$rc" = 2 ] && [ ! -e "$MIDI" ]'
done
env -u FF802_AUTO_ROOT "$SIM" > /dev/null 2>&1; rc=$?
ok "simulation refuses to run without its private root" '[ "$rc" = 2 ]'

D1SRC=$SRC/tools/hw-test/ff802_mixer_d1.c
code() { grep -vE "^ ?(\*|/\*)" "$D1SRC"; }
ok "source: exactly two SysEx literals, both for 0x4269 (values 0x0000 and 0x9000)" \
    '[ "$(code | grep -cE "\{ 0xF0(, 0x[0-9A-F]{2}){11} \}")" = 2 ] &&
     [ "$(code | grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x24, 0x13, 0x0C, 0xF7 }")" = 1 ] &&
     [ "$(code | grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x26, 0x13, 0x0C, 0xF7 }")" = 1 ] &&
     [ "$(code | grep -oE "0x4[0-9A-Fa-f]{3}u" | sort -u)" = "0x4269u" ] &&
     [ "$(code | grep -oE "\"(apply|restore)\", 0x[0-9A-F]{4}u" | tr "\n" ";")" = "\"apply\", 0x0000u;\"restore\", 0x9000u;" ]'
ok "source: no other message source (no mixer table, no free address or value)" \
    '! code | grep -Eq "mixer_init|ff802_rme_sysex_reg|strtoul|strtol|sscanf|atoi|0x4261|0x4228"'
ok "simulation build links neither libasound nor libusb" \
    '! "$NM" -D --undefined-only "$SIM" | grep -Eq " (snd_|libusb_)"'

# --- the real program (only if built) ----------------------------------------------
if [ "$REAL" != "-" ]; then
    "$REAL" --version > "$T/rv.txt" 2>&1; rc1=$?
    "$REAL" --help > /dev/null 2>&1; rc2=$?
    "$REAL" --apply --restore > /dev/null 2>&1; rc3=$?
    ok "real program: --version/--help, usage error before any device access" \
        '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$rc3" = 2 ] &&
         grep -Eq "^ff802-mixer-d1 [0-9.]+ \(ALSA sequencer, mixer writes enabled\)$" "$T/rv.txt"'
    und=$("$NM" -D --undefined-only "$REAL" | awk '{ print $2 }' | sed 's/@.*//')
    defs=$("$NM" "$REAL" | awk '{ print $3 }')
    has() { printf '%s\n' "$und" | grep -qx "$1"; }
    none_of() { for s in "$@"; do has "$s" && return 1; done; return 0; }
    ok "real program: sequencer subscribe and direct output linked (the write path of D1)" \
        'has snd_seq_open && has snd_seq_connect_to && has snd_seq_event_output_direct'
    ok "real program: no rawmidi, control writes, PCM, libusb" \
        'none_of snd_rawmidi_open snd_rawmidi_write snd_ctl_open snd_ctl_elem_write snd_pcm_open \
                 libusb_init libusb_open libusb_bulk_transfer'
    ok "real program: does not contain the 24-register table or the initialisation flow" \
        '! printf "%s\n" "$defs" | grep -Eqx "ff802_mixer_init_table|ff802_mixer_run|ff802_rme_sysex_reg|ff802_auto_handle"'
else
    echo "skip real program checks (FF802_BUILD_MIXER_D1=OFF)"
fi

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
