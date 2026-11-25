#!/bin/sh
# Manual mixer initialisation (ff802-mixer-init), without hardware:
#  - the simulation build (simulated MIDI backend, fake sysfs below a
#    private root): --list, --dry-run, --apply with the 168 messages byte
#    for byte and in order, confirmation, unit selection, lock, all
#    failures;
#  - the real program: only --help, --version, --list and usage errors
#    (nothing that reaches sysfs or ALSA), and its symbols with nm.
#
# Usage: test_mixer_init_tool.sh <source dir> <sim program> <real program or -> <nm> <scratch dir>

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
    */mi-scratch) ;;
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

# The 168 messages, written out (computed with an independent shell
# implementation of the parity and 7-bit split, not with the C codec).
EXPECTED="F0 00 20 0D 10 00 00 00 00 11 04 F7
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

dev() { # dev <name> <vid> <pid> <devnum>
    d=$DEVS/$1; mkdir -p "$d"
    echo "$2" > "$d/idVendor"; echo "$3" > "$d/idProduct"; echo 3 > "$d/busnum"
    echo "$4" > "$d/devnum"; echo 480 > "$d/speed"; echo RME > "$d/manufacturer"
    echo "Fireface 802 (24047128)" > "$d/product"
}
card() { mkdir -p "$DEVS/$1/$1:1.0/sound/card$2/midiC$2D0"; }
seqports() {
    cat > "$R/seq-ports" <<EOF
0 0 kernel -1 - Timer
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
mi() { # mi <stdin text> <args...>: print the exit code
    input=$1; shift
    printf '%s\n' "$input" | env FF802_AUTO_ROOT="$R" "$SIM" "$@" > "$T/out.txt" 2>&1
    echo $?
}
sysex() { n=$(grep -c '^sysex F0' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
connects() { n=$(grep -c '^seq connect' "$MIDI" 2>/dev/null); echo "${n:-0}"; }
sent_list() { grep '^sysex F0' "$MIDI" | sed 's/^sysex //'; }

# --- the table -------------------------------------------------------------------------
reset
rc=$(mi "" --list)
ok "--list: 168 messages, the expected bytes in order, no device access" \
    '[ "$rc" = 0 ] && [ "$(grep -cE "^ +[0-9]+  0x" "$T/out.txt")" = 168 ] &&
     [ "$(grep -E "^ +[0-9]+  0x" "$T/out.txt" | sed "s/.*\(F0 .*F7\)$/\1/")" = "$EXPECTED" ] &&
     grep -qx "configuration ff802-mixer-init-v4, 168 entries, checksum 0xC43D2879" "$T/out.txt" &&
     grep -qx "entries 1..38 unchanged from ff802-mixer-init-v3, checksum 0xEC858A44" "$T/out.txt" &&
     grep -qx "entries 1..26 unchanged from ff802-mixer-init-v2, checksum 0xC97A359C" "$T/out.txt" &&
     [ ! -e "$MIDI" ]'
ok "--list: v3 part (crossed off, routings, volumes, Input 9), then the 130 other crossed routings off" \
    '[ "$(grep -E "^ +[0-9]+  0x" "$T/out.txt" | awk "{ print \$2 \"=\" \$4 }" | tr "\n" " ")" = "0x4220=0x0000 0x4261=0x0000 0x4020=0x9000 0x4061=0x9000 0x40A2=0x9000 0x40E3=0x9000 0x4124=0x9000 0x4165=0x9000 0x41A6=0x9000 0x41E7=0x9000 0x4228=0x9000 0x4269=0x9000 0x42AA=0x9000 0x42EB=0x9000 0x1E00=0x0000 0x1F00=0x0000 0x2000=0x0000 0x2100=0x0000 0x2200=0x0000 0x2300=0x0000 0x2400=0x0000 0x2500=0x0000 0x2600=0x0000 0x2700=0x0000 0x2800=0x0000 0x2900=0x0000 0x4008=0x2666 0x4048=0x2666 0x4088=0x2666 0x40C8=0x2666 0x4108=0x2666 0x4148=0x2666 0x4188=0x2666 0x41C8=0x2666 0x4208=0x2666 0x4248=0x2666 0x4288=0x2666 0x42C8=0x2666 0x4021=0x0000 0x4022=0x0000 0x4023=0x0000 0x4024=0x0000 0x4025=0x0000 0x4026=0x0000 0x4027=0x0000 0x4028=0x0000 0x4029=0x0000 0x402A=0x0000 0x402B=0x0000 0x4060=0x0000 0x4062=0x0000 0x4063=0x0000 0x4064=0x0000 0x4065=0x0000 0x4066=0x0000 0x4067=0x0000 0x4068=0x0000 0x4069=0x0000 0x406A=0x0000 0x406B=0x0000 0x40A0=0x0000 0x40A1=0x0000 0x40A3=0x0000 0x40A4=0x0000 0x40A5=0x0000 0x40A6=0x0000 0x40A7=0x0000 0x40A8=0x0000 0x40A9=0x0000 0x40AA=0x0000 0x40AB=0x0000 0x40E0=0x0000 0x40E1=0x0000 0x40E2=0x0000 0x40E4=0x0000 0x40E5=0x0000 0x40E6=0x0000 0x40E7=0x0000 0x40E8=0x0000 0x40E9=0x0000 0x40EA=0x0000 0x40EB=0x0000 0x4120=0x0000 0x4121=0x0000 0x4122=0x0000 0x4123=0x0000 0x4125=0x0000 0x4126=0x0000 0x4127=0x0000 0x4128=0x0000 0x4129=0x0000 0x412A=0x0000 0x412B=0x0000 0x4160=0x0000 0x4161=0x0000 0x4162=0x0000 0x4163=0x0000 0x4164=0x0000 0x4166=0x0000 0x4167=0x0000 0x4168=0x0000 0x4169=0x0000 0x416A=0x0000 0x416B=0x0000 0x41A0=0x0000 0x41A1=0x0000 0x41A2=0x0000 0x41A3=0x0000 0x41A4=0x0000 0x41A5=0x0000 0x41A7=0x0000 0x41A8=0x0000 0x41A9=0x0000 0x41AA=0x0000 0x41AB=0x0000 0x41E0=0x0000 0x41E1=0x0000 0x41E2=0x0000 0x41E3=0x0000 0x41E4=0x0000 0x41E5=0x0000 0x41E6=0x0000 0x41E8=0x0000 0x41E9=0x0000 0x41EA=0x0000 0x41EB=0x0000 0x4221=0x0000 0x4222=0x0000 0x4223=0x0000 0x4224=0x0000 0x4225=0x0000 0x4226=0x0000 0x4227=0x0000 0x4229=0x0000 0x422A=0x0000 0x422B=0x0000 0x4260=0x0000 0x4262=0x0000 0x4263=0x0000 0x4264=0x0000 0x4265=0x0000 0x4266=0x0000 0x4267=0x0000 0x4268=0x0000 0x426A=0x0000 0x426B=0x0000 0x42A0=0x0000 0x42A1=0x0000 0x42A2=0x0000 0x42A3=0x0000 0x42A4=0x0000 0x42A5=0x0000 0x42A6=0x0000 0x42A7=0x0000 0x42A8=0x0000 0x42A9=0x0000 0x42AB=0x0000 0x42E0=0x0000 0x42E1=0x0000 0x42E2=0x0000 0x42E3=0x0000 0x42E4=0x0000 0x42E5=0x0000 0x42E6=0x0000 0x42E7=0x0000 0x42E8=0x0000 0x42E9=0x0000 0x42EA=0x0000 " ]'

# --- dry run ------------------------------------------------------------------------------
reset 20
rc=$(mi "" --dry-run)
ok "--dry-run: unit, card 1, port 20:1, nothing connected or sent" \
    '[ "$rc" = 0 ] && grep -q "^identity: Fireface 802, class-compliant mode" "$T/out.txt" &&
     grep -q "^mixer: MIDI port 20:1 .Fireface 802 (24047128) Port 2. of card 1" "$T/out.txt" &&
     grep -q "^result: dry-run (0 of 168 registers)" "$T/out.txt" &&
     [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ]'
reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 mi "" --dry-run)
ok "--dry-run also works in a build without writes" '[ "$rc" = 0 ] && grep -q "NOT compiled in" "$T/out.txt"'

# --- apply -------------------------------------------------------------------------------
for c in 20 24 129; do
    reset "$c"
    rc=$(mi SEND --apply)
    ok "--apply (client $c): one connection, exactly the 168 messages in order, then close" \
        '[ "$rc" = 0 ] && [ "$(connects)" = 1 ] && grep -qx "seq connect $c:1" "$MIDI" &&
         [ "$(sysex)" = 168 ] && [ "$(sent_list)" = "$EXPECTED" ] &&
         [ "$(tail -n 1 "$MIDI")" = "seq close" ] &&
         grep -q "^result: done (168 of 168 registers)" "$T/out.txt"'
done
ok "--apply: one log line per register" \
    '[ "$(grep -c "^mixer: set 0x" "$T/out.txt")" = 168 ] &&
     grep -q "^mixer: set 0x4220 = 0x0000 (Playback 1 -> Phones 9, off) \[1/168\]$" "$T/out.txt" &&
     grep -q "^mixer: set 0x4261 = 0x0000 (Playback 2 -> Phones 10, off) \[2/168\]$" "$T/out.txt" &&
     grep -q "^mixer: set 0x4269 = 0x9000 (Playback 10 -> Phones 10, 0 dB) \[12/168\]$" "$T/out.txt" &&
     grep -q "^mixer: set 0x4088 = 0x2666 (Input 9 -> Analog 3, 30 %) \[29/168\]$" "$T/out.txt" &&
     grep -q "^mixer: set 0x42C8 = 0x2666 (Input 9 -> Phones 12, 30 %) \[38/168\]$" "$T/out.txt" &&
     grep -q "^mixer: set 0x42A2 = 0x0000 (Playback 3 -> Phones 11, off) \[149/168\]$" "$T/out.txt" &&
     grep -q "^mixer: set 0x42EA = 0x0000 (Playback 11 -> Phones 12, off) \[168/168\]$" "$T/out.txt"'
reset 20
mi SEND --apply > /dev/null; first=$(sent_list)
rc=$(mi SEND --apply)
ok "--apply twice: the same 168 values again, nothing else" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 336 ] && [ "$(sent_list | head -n 168)" = "$first" ] &&
     [ "$(sent_list | tail -n 168)" = "$EXPECTED" ]'

for answer in "" "send" "yes" "SEND " "SENDSEND"; do
    reset 20
    rc=$(mi "$answer" --apply)
    ok "--apply confirmation '$answer': aborted, nothing connected or sent" \
        '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && [ "$(sysex)" = 0 ] && grep -q "^result: aborted" "$T/out.txt"'
done

reset 20
rc=$(FF802_SIM_MIXER_WRITE=0 mi SEND --apply)
ok "--apply in a build without writes: refused before asking" \
    '[ "$rc" = 3 ] && [ "$(connects)" = 0 ] && ! grep -q "Type SEND" "$T/out.txt" &&
     grep -q "^result: disabled-build" "$T/out.txt"'

# --- failures -------------------------------------------------------------------------
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=13 mi SEND --apply)
ok "message 13 fails: exit 4, 12 sent, stopped, no retry, connection closed" \
    '[ "$rc" = 4 ] && [ "$(sysex)" = 12 ] && [ "$(grep -c "^sysex FAILED" "$MIDI")" = 1 ] &&
     grep -q "^result: incomplete (12 of 168 registers)" "$T/out.txt" && grep -q "nothing retried" "$T/out.txt" &&
     [ "$(tail -n 1 "$MIDI")" = "seq close" ]'
reset 20
rc=$(FF802_SIM_SEQ_SEND_ERR_AT=1 mi SEND --apply)
ok "message 1 fails: exit 4, nothing else sent" '[ "$rc" = 4 ] && [ "$(sysex)" = 0 ]'
for f in FF802_SIM_SEQ_OPEN_ERR=1 FF802_SIM_SEQ_LIST_ERR=1 FF802_SIM_SEQ_CONNECT_ERR=busy; do
    reset 20
    rc=$(env $f FF802_AUTO_ROOT="$R" sh -c 'printf "SEND\n" | "$0" --apply' "$SIM" > "$T/out.txt" 2>&1; echo $?)
    ok "$f: exit 3, not-sent, nothing sent" \
        '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ] && grep -q "^result: not-sent" "$T/out.txt"'
done
reset 20; rm -f -- "$R/seq-ports"
rc=$(FF802_SIM_MIXER_TIMEOUT_MS=200 mi SEND --apply)
ok "no MIDI port: exit 3, no-port after the wait, nothing sent" \
    '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ] && grep -q "^result: no-port" "$T/out.txt"'
reset 20; rm -rf -- "${DEVS:?}/$PORT/$PORT:1.0"
rc=$(FF802_SIM_MIXER_TIMEOUT_MS=200 mi SEND --apply)
ok "no sound card: exit 3, no-port, sequencer not opened" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ]'
reset 20; seqports 20 "Fireface 802 (24047128) Port 1"
rc=$(mi SEND --apply)
ok "port 1 not named 'Port 2': refused, nothing sent" '[ "$rc" = 3 ] && [ "$(sysex)" = 0 ] && grep -q "^result: refused" "$T/out.txt"'

# --- unit selection ------------------------------------------------------------------
reset 20; rm -rf -- "${DEVS:?}/$PORT"
rc=$(mi SEND --apply)
ok "no Fireface 802: refused, nothing opened" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ] && grep -q "no original Fireface 802" "$T/out.txt"'
reset 20; dev "$PORT" 2a39 3fcd 53
rc=$(mi SEND --apply)
ok "unit in proprietary mode: refused, nothing opened" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ]'
rc=$(mi SEND --apply --connection "$PORT")
ok "named unit in proprietary mode: refused" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ] && grep -q "not an original Fireface 802 in class-compliant mode" "$T/out.txt"'
reset 20; dev 3-2.4.1.1.4 0424 3fdd 54; card 3-2.4.1.1.4 2
rc=$(mi SEND --apply)
ok "two units in CC mode: refused, never chosen silently" '[ "$rc" = 3 ] && [ ! -e "$MIDI" ] && grep -q "name one with --connection" "$T/out.txt"'
rc=$(mi SEND --apply --connection "$PORT")
ok "two units, one named: only that one (its card 1) is initialised" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 168 ] && grep -q "of card 1 for $PORT" "$T/out.txt"'
reset 20; dev 1-4 0424 3fdd 9; echo Microchip > "$DEVS/1-4/manufacturer"
rc=$(mi SEND --apply)
ok "other device with the CC USB ID: ignored, the Fireface is used" '[ "$rc" = 0 ] && [ "$(sysex)" = 168 ]'

# --- lock with the service -------------------------------------------------------------
reset 20; mkdir -p "$R/run/ff802-cc-auto"; : > "$R/run/ff802-cc-auto/lock"
( flock -x 9; sleep 1 ) 9< "$R/run/ff802-cc-auto/lock" &
holder=$!
sleep 0.2
start=$(date +%s%N)
rc=$(mi SEND --apply)
end=$(date +%s%N)
wait "$holder"
ok "the service lock is held: the manual run waits for it, then sends" \
    '[ "$rc" = 0 ] && [ "$(sysex)" = 168 ] && [ $(( (end - start) / 1000000 )) -ge 600 ] &&
     ! grep -q "no lock needed" "$T/out.txt"'
reset 20
rc=$(mi "" --dry-run)
ok "no lock file (service never ran): noted, run continues" '[ "$rc" = 0 ] && grep -q "no lock needed" "$T/out.txt"'

# --- usage ------------------------------------------------------------------------------
reset 20
for a in "" "--apply --dry-run" "--list --apply" "--apply now" "--register 0x4020" \
         "--value 0x9000" "--apply --connection" "--list --connection $PORT"; do
    # shellcheck disable=SC2086
    rc=$(mi SEND $a)
    ok "arguments '$a': usage error, nothing opened" '[ "$rc" = 2 ] && [ ! -e "$MIDI" ]'
done
env -u FF802_AUTO_ROOT "$SIM" --dry-run > /dev/null 2>&1; rc=$?
ok "simulation refuses to run without its private root" '[ "$rc" = 2 ]'
ok "simulation build links neither libasound nor libusb" \
    '! "$NM" -D --undefined-only "$SIM" | grep -Eq " (snd_|libusb_)"'
ok "source: no register address or value from the command line or literals" \
    '! grep -Eq "strtoul|strtol|sscanf|atoi|0x[0-9A-Fa-f]{4}u" "$SRC/tools/ff802_mixer_init.c"'

# --- the real program ---------------------------------------------------------------------
if [ "$REAL" != "-" ]; then
    "$REAL" --version > "$T/rv.txt" 2>&1; rc1=$?
    "$REAL" --help > /dev/null 2>&1; rc2=$?
    "$REAL" --list > "$T/rl.txt" 2>&1; rc3=$?
    "$REAL" --apply --dry-run > /dev/null 2>&1; rc4=$?
    ok "real program: --version/--help/--list, usage error; --list without device access" \
        '[ "$rc1" = 0 ] && [ "$rc2" = 0 ] && [ "$rc3" = 0 ] && [ "$rc4" = 2 ] &&
         grep -qx "configuration ff802-mixer-init-v4, 168 entries, checksum 0xC43D2879" "$T/rl.txt" &&
         grep -qx "entries 1..38 unchanged from ff802-mixer-init-v3, checksum 0xEC858A44" "$T/rl.txt" &&
         grep -qx "entries 1..26 unchanged from ff802-mixer-init-v2, checksum 0xC97A359C" "$T/rl.txt" &&
         [ "$(grep -E "^ +[0-9]+  0x" "$T/rl.txt" | sed "s/.*\(F0 .*F7\)$/\1/")" = "$EXPECTED" ]'
    und=$("$NM" -D --undefined-only "$REAL" | awk '{ print $2 }' | sed 's/@.*//')
    has() { printf '%s\n' "$und" | grep -qx "$1"; }
    none_of() { for s in "$@"; do has "$s" && return 1; done; return 0; }
    ok "real program: ALSA sequencer only (no rawmidi, control, PCM, libusb)" \
        'has snd_seq_open && none_of snd_rawmidi_open snd_ctl_open snd_ctl_elem_write snd_pcm_open libusb_init libusb_open'
    if grep -q "mixer writes enabled" "$T/rv.txt"; then
        ok "real program (writes enabled): subscribe and direct output linked" \
            'has snd_seq_connect_to && has snd_seq_event_output_direct'
    else
        ok "real program (writes disabled): no subscribe or output function linked" \
            'none_of snd_seq_connect_to snd_seq_create_simple_port snd_seq_event_output_direct'
    fi
else
    echo "skip real program checks (no alsa-lib)"
fi

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
