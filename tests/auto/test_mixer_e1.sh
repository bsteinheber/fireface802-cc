#!/bin/sh
# Hardware test E1 (ff802-mixer-e1), without hardware:
#  - the simulation build (simulated MIDI backend, fake sysfs below a
#    private root): dry run by default; the guided run off -> 0x2666 ->
#    listening check -> off; restore after every abort or error (end of
#    input, timeout, SIGINT, failed write); --restore; every refusal; the
#    log file;
#  - the real program, if built (FF802_BUILD_MIXER_E1=ON): only --help,
#    --version and usage errors, and its linked symbols with nm;
#  - the source: exactly the two messages for 0x4088.
#
# Usage: test_mixer_e1.sh <source dir> <sim program> <real program or -> <nm> <scratch dir>

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
    */e1-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
R=$T/root
PORT=3-2.4.1.1.3
DEVS=$R/sys/bus/usb/devices
MIDI=$R/midi.log
LOG=$T/e1.log
unset FF802_SIM_SEQ_OPEN_ERR FF802_SIM_SEQ_LIST_ERR FF802_SIM_SEQ_CONNECT_ERR \
      FF802_SIM_SEQ_SEND_ERR_AT FF802_SIM_MIXER_WRITE FF802_SIM_MIXER_TIMEOUT_MS \
      FF802_SIM_SEQ_PORTS_AFTER FF802_SIM_E1_LISTEN_S

# register, level and SysEx, computed here independently of the C code
sx() { a=$1; v=$2; w=$(( (a << 16) | v )); p=0; x=$w
       while [ "$x" -gt 0 ]; do p=$((p ^ (x & 1))); x=$((x >> 1)); done
       [ "$p" = 0 ] && w=$(( w | (1 << 31) ))
       printf 'F0 00 20 0D 10 00'
       for i in 0 1 2 3 4; do printf ' %02X' $(( (w >> (7 * i)) & 0x7F )); done
       printf ' F7\n'; }
REG=$(( 0x4000 | (2 << 6) | 8 ))                  # output index 2 (Analog 3), source 8 (Mic/Inst 9)
LEVEL=$(awk 'BEGIN { v = int(0.3 * 32768 + 0.5); if (v > 16384) v = int(v / 8) - 32768; printf "%d", v }')
OFF=$(sx "$REG" 0)
TEST=$(sx "$REG" "$LEVEL")

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
    rm -rf -- "${R:?}" "$LOG"
    mkdir -p "$DEVS"
    dev "$PORT" 0424 3fdd 53; card "$PORT" 1; seqports "${1:-20}"
}
e1() { # e1 <stdin text> <args...>: print the exit code
    input=$1; shift
    printf '%b' "$input" | env FF802_AUTO_ROOT="$R" "$SIM" "$@" > "$T/out.txt" 2>&1
    echo $?
}
run() { e1 "$1" --apply --log "$LOG"; }
sysex() { n=$(grep -c '^sysex ' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
connects() { n=$(grep -c '^seq connect' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
seq_of() { [ -f "$MIDI" ] && tr "\n" ";" < "$MIDI"; }
three="seq open;seq list;seq list;seq connect 20:1;sysex $OFF;sysex $TEST;sysex $OFF;seq close;"

# --- register and encoding --------------------------------------------------------
ok "register: Input 9 (source 8) -> Analog 3 (output 2) is 0x4088" '[ "$REG" = $((0x4088)) ]'
ok "level: OSCMix setmixlevel for 0.3 gives 0x2666 (lower, linear range)" '[ "$LEVEL" = $((0x2666)) ]'
ok "SysEx of 0x4088 = 0x0000 and 0x4088 = 0x2666 (independent shell computation)" \
    '[ "$OFF" = "F0 00 20 0D 10 00 00 00 20 04 04 F7" ] && [ "$TEST" = "F0 00 20 0D 10 00 66 4C 20 04 0C F7" ]'

# --- dry run (default) --------------------------------------------------------------
for a in "" "--dry-run"; do
    reset 20
    # shellcheck disable=SC2086
    rc=$(e1 "SEND\n1\n1\n1\n1\n" $a)
    ok "dry run '${a:-<no option>}': unit, card, port, both messages shown; nothing connected or sent, no log" \
        '[ "$rc" = 0 ] && grep -q "^ff802-mixer-e1 [0-9.]*: dry run, " "$T/out.txt" &&
         grep -q "^identity: Fireface 802, class-compliant mode" "$T/out.txt" &&
         grep -q "^selected port: 20:1 .Fireface 802 (24047128) Port 2.$" "$T/out.txt" &&
         grep -q "^message OFF:  register 0x4088 = 0x0000  off          word 0x40880000  SysEx $OFF$" "$T/out.txt" &&
         grep -q "^message TEST: register 0x4088 = 0x2666  30 % linear word 0xC0882666  SysEx $TEST$" "$T/out.txt" &&
         grep -q "not verified on the unit" "$T/out.txt" && grep -q "^dry run: nothing sent" "$T/out.txt" &&
         ! grep -q "Type SEND" "$T/out.txt" && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] &&
         [ "$(seq_of)" = "seq open;seq list;seq close;" ] && ! ls "$R"/ff802-mixer-e1-*.log "$T"/ff802-mixer-e1-*.log > /dev/null 2>&1'
done

# --- the guided test -------------------------------------------------------------------
reset 20
rc=$(run "SEND\n1\n1\n1\n1\n")
ok "passed: SEND, silent before, clearly audible, Out 4 silent, silent after: exit 0" \
    '[ "$rc" = 0 ] && grep -q "^result: PASSED" "$T/out.txt"'
ok "passed: exactly OFF, TEST, OFF over one subscription, connection checked again before it" \
    '[ "$(seq_of)" = "$three" ] && [ "$(connects)" = 1 ]'
ok "passed: plan, volume warning and listening prompt shown before/after the writes" \
    'grep -q "turn the monitoring volume" "$T/out.txt" && grep -q "^LISTEN NOW" "$T/out.txt" &&
     grep -q "no other matrix register, no input gain" "$T/out.txt"'
ok "log file: every step with time stamps, the answers and the result" \
    'grep -q "^log file: $LOG$" "$T/out.txt" && grep -q "approved by the user (SEND)" "$LOG" &&
     grep -q "\] step 1 OFF: 0x4088 = 0x0000  SysEx $OFF  transferred to ALSA" "$LOG" &&
     grep -q "\] step 2 TEST: 0x4088 = 0x2666  SysEx $TEST  transferred to ALSA" "$LOG" &&
     grep -q "\] step 3 RESTORE OFF: 0x4088 = 0x0000  SysEx $OFF  transferred to ALSA" "$LOG" &&
     grep -q "before the test: Input 9 on Out 3 silent" "$LOG" &&
     grep -q "listening check: Out 3 clearly audible; Out 4 silent" "$LOG" &&
     grep -q "after the restore: Out 3 silent" "$LOG" && grep -q "^result: PASSED" "$LOG" &&
     grep -q "closed (exit 0)" "$LOG"'
ok "MIDI success alone is never the result: the summary says transferred, no readback" \
    'grep -q "MIDI: OFF, TEST and RESTORE transferred to ALSA (no reply from the unit;" "$T/out.txt" &&
     grep -q "user check, not a measurement" "$T/out.txt"'

for case in "2:very faint" "3:nothing" "4:LOUD/DISTORTED"; do
    n=${case%%:*}; what=${case#*:}
    reset 20
    rc=$(run "SEND\n1\n$n\n1\n1\n")
    ok "answer '$what': not confirmed, exit 5, still restored (three messages)" \
        '[ "$rc" = 5 ] && [ "$(seq_of)" = "$three" ] && grep -q "^result: NOT CONFIRMED" "$T/out.txt" &&
         grep -q "listening check: Out 3 $what" "$LOG"'
done
reset 20
rc=$(run "SEND\n1\n1\n2\n1\n")
ok "Input 9 also on Out 4: not confirmed, exit 5, restored" '[ "$rc" = 5 ] && [ "$(seq_of)" = "$three" ]'
reset 20
rc=$(run "SEND\n2\n1\n1\n1\n")
ok "audible before the test: recorded, then replaced by off; test runs" \
    '[ "$rc" = 0 ] && grep -q "AUDIBLE (previous value not off; it will be replaced by off)" "$LOG"'
reset 20
rc=$(run "SEND\n1\nx\n9\n1\n1\n1\n")
ok "invalid answers are asked again, nothing extra sent" '[ "$rc" = 0 ] && [ "$(sysex)" = 3 ]'
reset 20
rc=$(run "SEND\n1\n1\n1\n2\n")
ok "still audible after the restore: exit 6, explicit warning" \
    '[ "$rc" = 6 ] && grep -q "still audible after the restore" "$T/out.txt" && [ "$(sysex)" = 3 ]'

# --- aborts before any write ------------------------------------------------------
for answer in "" "send" "yes" "SEND " "SENDX"; do
    reset 20
    rc=$(run "$answer\n1\n1\n1\n1\n")
    ok "confirmation '$answer': aborted, no connect, nothing sent" \
        '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && grep -q "aborted: nothing sent" "$T/out.txt"'
done
reset 20
rc=$(run "SEND\n")
ok "end of input at the question before the test: nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'

# --- restore after aborts and errors ------------------------------------------------
reset 20
rc=$(run "SEND\n1\n")
ok "end of input during the listening check: restored (OFF sent), exit 5" \
    '[ "$rc" = 5 ] && [ "$(seq_of)" = "$three" ] && grep -q "listening check ended early (end of input); restoring" "$LOG"'
reset 20
rc=$( (printf 'SEND\n1\n'; sleep 4) | env FF802_AUTO_ROOT="$R" FF802_SIM_E1_LISTEN_S=1 "$SIM" --apply --log "$LOG" > "$T/out.txt" 2>&1; echo $?)
ok "no answer within the time limit: restored by itself, exit 5" \
    '[ "$rc" = 5 ] && [ "$(seq_of)" = "$three" ] && grep -q "no answer within the time limit); restoring" "$LOG"'
reset 20
mkfifo "$T/in"
env FF802_AUTO_ROOT="$R" "$SIM" --apply --log "$LOG" < "$T/in" > "$T/out.txt" 2>&1 &
pid=$!
exec 3> "$T/in"
printf 'SEND\n1\n' >&3
n=0; while ! grep -q "^LISTEN NOW" "$T/out.txt" 2>/dev/null && [ $n -lt 50 ]; do sleep 0.1; n=$((n + 1)); done
kill -INT "$pid"
sleep 0.5
exec 3>&-
wait "$pid"; rc=$?
rm -f "$T/in"
ok "Ctrl-C (SIGINT) during the listening check: restored, exit 5" \
    '[ "$rc" = 5 ] && [ "$(seq_of)" = "$three" ] && grep -q "interrupted by a signal); restoring" "$LOG"'

reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 run "SEND\n1\n1\n1\n1\n")
ok "step 1 (OFF) fails: exit 4, nothing else sent" \
    '[ "$rc" = 4 ] && [ "$(sysex)" = 1 ] && grep -q "FAILED at step 1; nothing else sent" "$T/out.txt" &&
     grep -q "step 1 OFF: .* FAILED .*; not retried" "$LOG"'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=2 run "SEND\n1\n1\n1\n1\n")
ok "step 2 (TEST) fails: no listening check, restore sent, exit 4" \
    '[ "$rc" = 4 ] && [ "$(sysex)" = 3 ] && ! grep -q "^LISTEN NOW" "$T/out.txt" &&
     grep -q "FAILED at step 2 (test value not transferred); restored to off" "$T/out.txt"'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=3 run "SEND\n1\n1\n1\n1\n")
ok "restore fails: exit 6, explicit RESTORE FAILED with the recovery command, not retried" \
    '[ "$rc" = 6 ] && [ "$(sysex)" = 3 ] && grep -q "RESTORE FAILED: 0x4088 may still be 0x2666" "$T/out.txt" &&
     grep -q "ff802-mixer-e1 --restore" "$T/out.txt" && grep -q "RESTORE FAILED" "$LOG"'
reset 20
rc=$(FF802_SIM_SEQ_CONNECT_ERR=busy run "SEND\n1\n1\n1\n1\n")
ok "connect fails (busy): exit 3, nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 run "SEND\n1\n1\n1\n1\n")
ok "program without write functions: refused before asking, nothing sent" \
    '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && ! grep -q "Type SEND" "$T/out.txt"'
reset 20
rc=$(env FF802_AUTO_ROOT="$R" "$SIM" --apply --log "$T/no/such/dir/e1.log" < /dev/null > "$T/out.txt" 2>&1; echo $?)
ok "log file cannot be created: refused before anything is opened" \
    '[ "$rc" = 3 ] && [ ! -e "$MIDI" ] && grep -q "cannot open the log file" "$T/out.txt"'

# --- --restore -----------------------------------------------------------------------
reset 20
rc=$(e1 "SEND\n" --restore --log "$LOG")
ok "--restore: exactly one message 0x4088 = 0x0000, exit 0" \
    '[ "$rc" = 0 ] && [ "$(seq_of)" = "seq open;seq list;seq list;seq connect 20:1;sysex $OFF;seq close;" ] &&
     grep -q "restore transferred (0x4088 = 0x0000)" "$LOG"'
reset 20
rc=$(e1 "no\n" --restore --log "$LOG")
ok "--restore without SEND: nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 e1 "SEND\n" --restore --log "$LOG")
ok "--restore fails: exit 6, RESTORE FAILED" '[ "$rc" = 6 ] && grep -q "RESTORE FAILED" "$T/out.txt"'

# --- refusals: wrong device or mode ------------------------------------------------
refused() { # refused <description>: exit 3, no connect, nothing sent
    rc=$(run "SEND\n1\n1\n1\n1\n")
    ok "$1: refused, nothing sent" '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ]'
}
reset 20; rm -rf -- "${DEVS:?}/$PORT"; refused "no Fireface 802"
reset 20; dev "$PORT" 2a39 3fcd 53; refused "unit in proprietary mode"
reset 20; echo "Fireface 802 FS (1)" > "$DEVS/$PORT/product"; refused "Fireface 802 FS"
reset 20; dev 3-2.4.1.1.4 0424 3fdd 54; card 3-2.4.1.1.4 2; refused "two units in CC mode without --connection"
reset 20; rm -rf -- "${DEVS:?}/$PORT/$PORT:1.0"; refused "no sound card"
reset 20; rm -f -- "$R/seq-ports"; refused "no sequencer port"
reset 20; seqports 20 "Fireface 802 (24047128) Port 1"; refused "port 1 not named 'Port 2'"
reset 20; rc=$(e1 "SEND\n1\n1\n1\n1\n" --apply --log "$LOG" --connection "$PORT")
ok "--connection to the unit: the guided test runs" '[ "$rc" = 0 ] && [ "$(sysex)" = 3 ]'

# --- no other register or value -------------------------------------------------------
reset 20
for a in "--apply --apply" "--apply --restore" "--restore --dry-run" "--apply 0x4088" "--register 0x4088" \
         "--value 0x9000" "--level 0.5" "--refresh" "--log x" "--dry-run --log x" "--connection" "extra"; do
    # shellcheck disable=SC2086
    rc=$(e1 "SEND\n" $a)
    ok "arguments '$a': usage error, nothing opened" '[ "$rc" = 2 ] && [ ! -e "$MIDI" ]'
done
env -u FF802_AUTO_ROOT "$SIM" > /dev/null 2>&1; rc=$?
ok "simulation refuses to run without its private root" '[ "$rc" = 2 ]'

E1SRC=$SRC/tools/hw-test/ff802_mixer_e1.c
code() { grep -vE "^ ?(\*|/\*)" "$E1SRC"; }
ok "source: exactly two SysEx literals, OFF and TEST of 0x4088" \
    '[ "$(code | grep -cE "\{ 0xF0(, 0x[0-9A-F]{2}){11} \}")" = 2 ] &&
     [ "$(code | grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x20, 0x04, 0x04, 0xF7 }")" = 1 ] &&
     [ "$(code | grep -c "{ 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x66, 0x4C, 0x20, 0x04, 0x0C, 0xF7 }")" = 1 ] &&
     [ "$(code | grep -oE "0x[0-9A-Fa-f]{4}u" | sort -u | tr "\n" " ")" = "0x0000u 0x2666u 0x4088u " ] &&
     grep -q "^#define E1_REG        0x4088u$" "$E1SRC" &&
     grep -q "^#define E1_OFF        0x0000u$" "$E1SRC" && grep -q "^#define E1_TEST       0x2666u$" "$E1SRC"'
ok "source: no other message source (no table, no free value, no refresh, no output volume)" \
    '! code | grep -Eq "mixer_init|ff802_rme_sysex_reg|strtol\(|sscanf|atoi|0x3F99|0x3f99|0x0812|0x9000|0x1E00|0x2000"'
ok "simulation build links neither libasound nor libusb" \
    '! "$NM" -D --undefined-only "$SIM" | grep -Eq " (snd_|libusb_)"'

# --- the real program (only if built) ----------------------------------------------
if [ "$REAL" != "-" ]; then
    "$REAL" --version > "$T/rv.txt" 2>&1; rc1=$?
    "$REAL" --help > /dev/null 2>&1; rc2=$?
    "$REAL" --apply --apply > /dev/null 2>&1; rc3=$?
    ok "real program: --version/--help, usage error before any device access" \
        '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$rc3" = 2 ] &&
         grep -Eq "^ff802-mixer-e1 [0-9.]+ \(ALSA sequencer, mixer writes enabled\)$" "$T/rv.txt"'
    und=$("$NM" -D --undefined-only "$REAL" | awk '{ print $2 }' | sed 's/@.*//')
    defs=$("$NM" "$REAL" | awk '{ print $3 }')
    has() { printf '%s\n' "$und" | grep -qx "$1"; }
    none_of() { for s in "$@"; do has "$s" && return 1; done; return 0; }
    ok "real program: sequencer subscribe and direct output linked" \
        'has snd_seq_open && has snd_seq_connect_to && has snd_seq_event_output_direct'
    ok "real program: no rawmidi, control writes, PCM, libusb" \
        'none_of snd_rawmidi_open snd_rawmidi_write snd_ctl_open snd_ctl_elem_write snd_pcm_open \
                 libusb_init libusb_open libusb_bulk_transfer'
    ok "real program: does not contain the register table or the initialisation flow" \
        '! printf "%s\n" "$defs" | grep -Eqx "ff802_mixer_init_table|ff802_mixer_run|ff802_rme_sysex_reg|ff802_auto_handle"'
else
    echo "skip real program checks (FF802_BUILD_MIXER_E1=OFF)"
fi

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
