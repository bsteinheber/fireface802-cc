#!/bin/sh
# Simulated test of tools/hw-test/ff802-ucm-t1.sh.
#
# Uses a fake system UCM tree, a fake /proc/asound and a fake alsaucm that
# records every call. Nothing outside the given scratch directory is used.
#
# Usage: test_ucm_t1.sh <project source dir> <scratch dir>

set -u

SRC=$1
T=$2
SCRIPT=$SRC/tools/hw-test/ff802-ucm-t1.sh
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
    */t1-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"

# --- fake system UCM tree --------------------------------------------------
SYS=$T/sys-ucm2
mkdir -p "$SYS/USB-Audio/RME" "$SYS/common/pcm" "$SYS/conf.d/USB-Audio"
cat > "$SYS/USB-Audio/USB-Audio.conf" <<'EOF'
Syntax 8
If.opt.Append.Include.opt {
	File "/USB-Audio/conf.d/${var:USBID1}-${var:USBID2}.conf"
	Optional true
}
EOF
echo "# split macros" > "$SYS/common/pcm/split.conf"
echo "Comment \"UCX\"" > "$SYS/USB-Audio/RME/Fireface-UCX.conf"
ln -s ../../USB-Audio/RME/Fireface-UCX.conf "$SYS/conf.d/USB-Audio/ucx.conf"
sys_sum() { (cd "$SYS" && find . -print | sort && find . -type f -exec cksum {} + | sort); }
sys_before=$(sys_sum)

# --- fake /proc/asound -------------------------------------------------------
PROC=$T/proc-asound
make_proc() {
    rm -rf -- "${PROC:?}"
    mkdir -p "$PROC/card0" "$PROC/card1"
    echo "1d6b:0002" > "$PROC/card0/usbid.unused"
    echo "0424:3fdd" > "$PROC/card1/usbid"
    cat > "$PROC/cards" <<EOF
 0 [PCH            ]: HDA-Intel - HDA Intel PCH
                      HDA Intel PCH at 0xf7f10000 irq 30
 1 [F802           ]: USB-Audio - $1
                      $2 at usb-0000:00:14.0-2.4.1.1.3, high speed
EOF
}

# --- fake alsaucm --------------------------------------------------------------
FAKE=$T/alsaucm
LOG=$T/alsaucm.log
cat > "$FAKE" <<'EOF'
#!/bin/sh
echo "UCM2=${ALSA_CONFIG_UCM2:-unset} ARGS=$*" >> "$FAKE_LOG"
for a in "$@"; do
    case $a in set|reset|_boot|_fboot|_verb|_enadev|_disdev) echo "forbidden command $a" >> "$FAKE_LOG"; exit 9 ;; esac
done
if [ -z "${ALSA_CONFIG_UCM2:-}" ]; then
    echo "ALSA lib: UCM is not supported for this USB device" >&2
    exit 1
fi
[ -f "$ALSA_CONFIG_UCM2/USB-Audio/RME/Fireface-802-HiFi.conf" ] || exit 7
case "$*" in
    *"list _verbs"*)      echo "  0: HiFi"; echo "    Default" ;;
    *"list _devices/HiFi"*)
        for d in "Headphones 1" "Headphones 2" "Line 1" "Line 2" "Line 3" "Line 4" "Line 5"; do
            [ "$d" = "${FAKE_DROP_DEVICE:-}" ] || echo "  x: $d"
        done ;;
    *"<<<SplitPCM=1>>>"*"dump text"*)
        echo 'Device "Headphones 1"'
        echo '  PlaybackChannels "22"'
        echo '  PlaybackPCM "hw:F802,0"'
        echo '  CaptureChannels "22"' ;;
    *"dump text"*)
        echo '  PlaybackPCM "ff802_stereo_out:F802,0,8,9"'
        echo '  CapturePCM "ff802_stereo_in:F802,0,0,1"' ;;
    *) exit 5 ;;
esac
EOF
chmod +x "$FAKE"

W=$T/work
export FF802_T1_DIR="$W" FF802_T1_SYSTEM_UCM2="$SYS" FF802_T1_PROC_ASOUND="$PROC" \
       FF802_T1_ALSAUCM="$FAKE" FAKE_LOG="$LOG"
unset FF802_T1_REPO_UCM2 FAKE_DROP_DEVICE

run_t1() { sh "$SCRIPT" "$@" > "$T/out.txt" 2>&1; echo $?; }

# --- 1. syntax -----------------------------------------------------------------
ok "script passes 'sh -n'" 'sh -n "$SCRIPT"'

# --- 2. prepare ------------------------------------------------------------------
make_proc "Fireface 802 (24047128)" "RME Fireface 802 (24047128)"
rc=$(run_t1 prepare)
ok "prepare succeeds" '[ "$rc" = 0 ]'
ok "marker file created" '[ -f "$W/.ff802-ucm-t1" ]'
for rel in USB-Audio/conf.d/0424-3fdd.conf USB-Audio/RME/Fireface-802.conf USB-Audio/RME/Fireface-802-HiFi.conf; do
    ok "overlay contains $rel unchanged" 'cmp -s "$SRC/audio/ucm2/$rel" "$W/ucm2/$rel"'
done
ok "system files copied into overlay" '[ -f "$W/ucm2/USB-Audio/RME/Fireface-UCX.conf" ]'
ok "symlinks are preserved" '[ -L "$W/ucm2/conf.d/USB-Audio/ucx.conf" ]'
ok "manifest written" 'grep -q "Fireface-802-HiFi.conf" "$W/manifest.txt"'
ok "system tree unchanged after prepare" '[ "$(sys_sum)" = "$sys_before" ]'

# --- 3. run --------------------------------------------------------------------
: > "$LOG"
rc=$(run_t1 run)
ok "run succeeds" '[ "$rc" = 0 ]'
ok "all checks reported PASS" '! grep -q "FAIL" "$T/out.txt" && grep -q "all checks passed" "$T/out.txt"'
ok "card hw:1 detected" 'grep -q "ARGS=-c hw:1 list _verbs" "$LOG"'
ok "baseline call uses the unmodified system tree" 'grep -q "^UCM2=unset ARGS=-c hw:1 list _verbs" "$LOG"'
ok "profile calls use the private overlay" '[ "$(grep -c "^UCM2=$W/ucm2 " "$LOG")" = 4 ]'
ok "SplitPCM variant queried" 'grep -q "ARGS=-c <<<SplitPCM=1>>>hw:1 dump text" "$LOG"'
ok "no state-changing alsaucm command" '! grep -q "forbidden command" "$LOG"'
ok "exactly five alsaucm calls" '[ "$(wc -l < "$LOG")" -eq 5 ]'
ok "system tree unchanged after run" '[ "$(sys_sum)" = "$sys_before" ]'

# --- 4. failing checks are reported ------------------------------------------
FAKE_DROP_DEVICE="Line 5"; export FAKE_DROP_DEVICE
rc=$(run_t1 run)
ok "missing device makes run fail (exit 1)" '[ "$rc" = 1 ] && grep -q "FAIL  device .Line 5." "$T/out.txt"'
unset FAKE_DROP_DEVICE

make_proc "Fireface 802 FS (1234)" "RME Fireface 802 FS (1234)"
rc=$(run_t1 run)
ok "802 FS long name makes run fail" '[ "$rc" = 1 ]'

make_proc "x" "x"
rm -f "$PROC/card1/usbid"
rc=$(run_t1 run)
ok "no Fireface card: exit 3" '[ "$rc" = 3 ]'

make_proc "Fireface 802 (1)" "RME Fireface 802 (1)"
mkdir -p "$PROC/card2"; echo "0424:3fdd" > "$PROC/card2/usbid"
rc=$(run_t1 run)
ok "two Fireface cards: refused" '[ "$rc" = 2 ]'
make_proc "Fireface 802 (24047128)" "RME Fireface 802 (24047128)"

# --- 5. clean --------------------------------------------------------------------
rc=$(run_t1 clean)
ok "clean succeeds" '[ "$rc" = 0 ] && [ ! -e "$W" ]'
rc=$(run_t1 run)
ok "run without prepare is refused" '[ "$rc" = 2 ]'
ok "system tree unchanged after clean" '[ "$(sys_sum)" = "$sys_before" ]'

# --- 6. guards ---------------------------------------------------------------------
mkdir -p "$W"; echo keep > "$W/foreign.txt"
rc=$(run_t1 prepare)
ok "foreign working dir is not touched" '[ "$rc" = 2 ] && [ -f "$W/foreign.txt" ]'
rc=$(run_t1 clean)
ok "clean refuses foreign working dir" '[ "$rc" = 2 ] && [ -f "$W/foreign.txt" ]'
rm -rf -- "${W:?}"

mkdir -p "$SYS/USB-Audio/conf.d"; echo "x" > "$SYS/USB-Audio/conf.d/0424-3fdd.conf"
rc=$(run_t1 prepare)
ok "existing system profile file aborts prepare" '[ "$rc" = 2 ] && [ ! -e "$W/ucm2" ]'
rm -f "$SYS/USB-Audio/conf.d/0424-3fdd.conf"; rmdir "$SYS/USB-Audio/conf.d"

cp "$SYS/USB-Audio/USB-Audio.conf" "$T/usb-audio.bak"
echo "Syntax 8" > "$SYS/USB-Audio/USB-Audio.conf"
rc=$(run_t1 prepare)
ok "missing conf.d hook aborts prepare" '[ "$rc" = 2 ]'
cp "$T/usb-audio.bak" "$SYS/USB-Audio/USB-Audio.conf"

BAD=$T/bad-repo
mkdir -p "$BAD"; cp -R "$SRC/audio/ucm2" "$BAD/"
printf '\nSectionDefaults [ cset "name=x 1" ]\n' >> "$BAD/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf"
rc=$(FF802_T1_REPO_UCM2=$BAD/ucm2 sh "$SCRIPT" prepare > "$T/out.txt" 2>&1; echo $?)
ok "control write directive aborts prepare" '[ "$rc" = 2 ] && grep -q "unexpected control/exec directive" "$T/out.txt"'
rm -rf -- "${W:?}"

ok "system tree unchanged at the end" '[ "$(sys_sum)" = "$sys_before" ]'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
