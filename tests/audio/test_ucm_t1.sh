#!/bin/sh
# Simulated test of tools/hw-test/ff802-ucm-t1.sh and of the host wrapper
# tools/hw-test/ff802-ucm-t1-host.sh.
#
# Uses a fake system UCM tree, a fake /proc/asound and a fake alsaucm that
# records every call and answers with the real alsa-lib output stored in
# tests/audio/fixtures/t1 (kept up to date by test_ucm_eval.sh). Like
# alsa-lib 1.2.15, the fake only finds the profile if the overlay's
# USB-Audio.conf contains the loader block inserted by 'prepare'.
# Nothing outside the given scratch directory is written.
#
# Usage: test_ucm_t1.sh <project source dir> <scratch dir>

set -u

SRC=$1
T=$2
SCRIPT=$SRC/tools/hw-test/ff802-ucm-t1.sh
WRAP=$SRC/tools/hw-test/ff802-ucm-t1-host.sh
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

If.inc {
	Condition {
		Type String
		Empty "${var:ProfileName}"
	}
	True.Error "UCM is not supported for this USB device"
	False.Include.prof.File "/USB-Audio/${var:ProfileName}.conf"
}
EOF
echo "# split macros" > "$SYS/common/pcm/split.conf"
echo "Comment \"UCX\"" > "$SYS/USB-Audio/RME/Fireface-UCX.conf"
ln -s ../../USB-Audio/USB-Audio.conf "$SYS/conf.d/USB-Audio/USB-Audio.conf"
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
 1 [F80224047128   ]: USB-Audio - $1
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
unsupported() { cat "$FAKE_FIXTURES/baseline-verbs.txt" >&2; exit 1; }
out() {
    if [ -n "${FAKE_MUTATE:-}" ]; then sed "$FAKE_MUTATE" "$FAKE_FIXTURES/$1"; else cat "$FAKE_FIXTURES/$1"; fi
}
tree=${ALSA_CONFIG_UCM2:-unset}
[ "$tree" != unset ] && [ "$tree" != "$FAKE_SYS" ] || unsupported
[ -f "$tree/USB-Audio/RME/Fireface-802-HiFi.conf" ] || exit 7
# alsa-lib 1.2.15 never loads the optional conf.d include: the profile is
# found only through the loader block or a long-name master file
grep -q '^If\.ff802-t1 {' "$tree/USB-Audio/USB-Audio.conf" ||
    [ -n "$(find "$tree/conf.d/USB-Audio" -name 'RME Fireface 802 (*.conf' 2>/dev/null)" ] ||
    unsupported
[ -z "${FAKE_WARN:-}" ] || echo "ALSA lib parser.c:1:(x) [warning.ucm] simulated warning" >&2
case "$*" in
    *"list _verbs"*)                out verbs.txt ;;
    *"list _devices/HiFi"*)         out devices.txt ;;
    *"<<<SplitPCM=1>>>"*"dump text"*) out dump-split.txt ;;
    *"dump text"*)                  out dump.txt ;;
    *) exit 5 ;;
esac
EOF
chmod +x "$FAKE"

W=$T/work
export FF802_T1_DIR="$W" FF802_T1_SYSTEM_UCM2="$SYS" FF802_T1_PROC_ASOUND="$PROC" \
       FF802_T1_ALSAUCM="$FAKE" FAKE_LOG="$LOG" FAKE_SYS="$SYS" \
       FAKE_FIXTURES="$SRC/tests/audio/fixtures/t1" FF802_T1_OUT="$T/host-out"
unset FF802_T1_REPO_UCM2 FAKE_MUTATE FAKE_WARN ALSA_CONFIG_UCM2

run_t1() { sh "$SCRIPT" "$@" > "$T/out.txt" 2>&1; echo $?; }

# --- 1. syntax -----------------------------------------------------------------
ok "script passes 'sh -n'" 'sh -n "$SCRIPT"'
ok "host wrapper passes 'bash -n'" 'bash -n "$WRAP"'

# --- 2. prepare ------------------------------------------------------------------
make_proc "Fireface 802 (24047128)" "RME Fireface 802 (24047128)"
rc=$(run_t1 prepare)
ok "prepare succeeds" '[ "$rc" = 0 ]'
ok "marker file created" '[ -f "$W/.ff802-ucm-t1" ]'
for rel in USB-Audio/conf.d/0424-3fdd.conf USB-Audio/RME/Fireface-802.conf USB-Audio/RME/Fireface-802-HiFi.conf; do
    ok "overlay contains $rel unchanged" 'cmp -s "$SRC/audio/ucm2/$rel" "$W/ucm2/$rel"'
done
ok "system files copied into overlay" '[ -f "$W/ucm2/USB-Audio/RME/Fireface-UCX.conf" ]'
ok "symlinks are preserved" '[ -L "$W/ucm2/conf.d/USB-Audio/USB-Audio.conf" ]'
ok "loader block inserted once, directly before If.inc" \
    '[ "$(grep -c "^If\.ff802-t1 {" "$W/ucm2/USB-Audio/USB-Audio.conf")" = 1 ] &&
     awk "/^If\.ff802-t1 \{/{a=NR} /^If\.inc[ \t]*\{/{b=NR} END{exit !(a && b && a < b)}" "$W/ucm2/USB-Audio/USB-Audio.conf"'
ok "loader includes conf.d file without Optional, guarded by USB id" \
    'grep -q "True.Include.ff802.File \"/USB-Audio/conf.d/0424-3fdd.conf\"" "$W/ucm2/USB-Audio/USB-Audio.conf" &&
     grep -q "Needle \"USB0424:3fdd\"" "$W/ucm2/USB-Audio/USB-Audio.conf"'
ok "overlay USB-Audio.conf otherwise identical to the system file" \
    '[ "$(diff "$SYS/USB-Audio/USB-Audio.conf" "$W/ucm2/USB-Audio/USB-Audio.conf" | grep -c "^<")" = 0 ]'
ok "manifest written" 'grep -q "Fireface-802-HiFi.conf" "$W/manifest.txt" && grep -q "If.ff802-t1 inserted" "$W/manifest.txt"'
ok "system tree unchanged after prepare" '[ "$(sys_sum)" = "$sys_before" ]'

# --- 3. run --------------------------------------------------------------------
: > "$LOG"
rc=$(run_t1 run)
ok "run succeeds" '[ "$rc" = 0 ]'
ok "all checks reported PASS" '! grep -q "FAIL" "$T/out.txt" && grep -q "all checks passed" "$T/out.txt"'
ok "every expected device checked" '[ "$(grep -c "SplitPCM gives hw PCM" "$T/out.txt")" = 7 ]'
ok "card hw:1 detected" 'grep -q "ARGS=-c hw:1 list _verbs" "$LOG"'
ok "real long name (second line) recorded" 'grep -qx "long name: RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed" "$W/results/card.txt"'
ok "baseline call uses the unmodified system tree" 'grep -q "^UCM2=$SYS ARGS=-c hw:1 list _verbs" "$LOG"'
ok "profile calls use the private overlay" '[ "$(grep -c "^UCM2=$W/ucm2 " "$LOG")" = 4 ]'
ok "SplitPCM variant queried" 'grep -q "ARGS=-c <<<SplitPCM=1>>>hw:1 dump text" "$LOG"'
ok "no state-changing alsaucm command" '! grep -q "forbidden command" "$LOG"'
ok "exactly five alsaucm calls" '[ "$(wc -l < "$LOG")" -eq 5 ]'
ok "system tree unchanged after run" '[ "$(sys_sum)" = "$sys_before" ]'

# --- 4. failing checks are reported ------------------------------------------
fails_with() { # fails_with <description> <sed mutation> <expected FAIL text>
    FAKE_MUTATE=$2; export FAKE_MUTATE
    expect=$3
    rc=$(run_t1 run)
    ok "$1 makes run fail" '[ "$rc" = 1 ] && grep -q "FAIL  .*$expect" "$T/out.txt"'
    unset FAKE_MUTATE
}
fails_with "missing device" '/^  [0-9]*: Line4$/{N;d;}' "exactly 7 devices"
fails_with "wrong channel pair" 's/ff802_stereo_out:\([^,]*\),0,8,9/ff802_stereo_out:\1,0,9,8/' "Headphones1: PlaybackPCM"
fails_with "wrong comment" 's/Analog Input 1\/2/Analog Input 3\/4/' "Line5: listed"
fails_with "wrong priority" 's/PlaybackPriority 255/PlaybackPriority 100/' "Headphones1: PlaybackPCM"
fails_with "SplitPCM with 2 instead of 22 channels" 's/Channels 22/Channels 2/' "SplitPCM gives hw PCM"
fails_with "SplitPCM channel position" 's/CaptureChannelPos1 FR/CaptureChannelPos1 FL/' "Line5: SplitPCM"
fails_with "second verb" 's/^    Default$/    Default\n  1: Other/' "HiFi is the only verb"

FAKE_WARN=1; export FAKE_WARN
rc=$(run_t1 run)
ok "ALSA warning makes run fail" '[ "$rc" = 1 ] && grep -q "FAIL  verbs: alsaucm succeeds without ALSA errors or warnings" "$T/out.txt"'
unset FAKE_WARN

mv "$W/ucm2/USB-Audio/USB-Audio.conf" "$T/overlay-usb-audio.bak"
cp "$SYS/USB-Audio/USB-Audio.conf" "$W/ucm2/USB-Audio/USB-Audio.conf"
rc=$(run_t1 run)
ok "overlay without loader (alsa-lib 1.2.15 behaviour) makes run fail" \
    '[ "$rc" = 1 ] && grep -q "FAIL  HiFi is the only verb" "$T/out.txt"'
mv "$T/overlay-usb-audio.bak" "$W/ucm2/USB-Audio/USB-Audio.conf"

make_proc "F802" "RME Fireface 802 (24047128)"
rc=$(run_t1 run)
ok "long name is read from the second line (short name irrelevant)" '[ "$rc" = 0 ]'

make_proc "Fireface 802 (1234)" "RME Fireface 802 FS (1234)"
rc=$(run_t1 run)
ok "802 FS long name makes run fail" '[ "$rc" = 1 ] && grep -q "FAIL  card long name" "$T/out.txt"'

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
grep -v '^If\.inc' "$T/usb-audio.bak" > "$SYS/USB-Audio/USB-Audio.conf"
rc=$(run_t1 prepare)
ok "missing If.inc aborts prepare" '[ "$rc" = 2 ] && grep -q "exactly one .If.inc" "$T/out.txt"'
cat "$T/usb-audio.bak" "$T/usb-audio.bak" > "$SYS/USB-Audio/USB-Audio.conf"
rc=$(run_t1 prepare)
ok "two If.inc blocks abort prepare" '[ "$rc" = 2 ] && grep -q "exactly one .If.inc" "$T/out.txt"'
cp "$T/usb-audio.bak" "$SYS/USB-Audio/USB-Audio.conf"
rm -rf -- "${W:?}"

BAD=$T/bad-repo
mkdir -p "$BAD"; cp -R "$SRC/audio/ucm2" "$BAD/"
printf '\nSectionDefaults [ cset "name=x 1" ]\n' >> "$BAD/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf"
rc=$(FF802_T1_REPO_UCM2=$BAD/ucm2 sh "$SCRIPT" prepare > "$T/out.txt" 2>&1; echo $?)
ok "control write directive aborts prepare" '[ "$rc" = 2 ] && grep -q "unexpected control/exec directive" "$T/out.txt"'
rm -rf -- "${W:?}"

ok "system tree unchanged" '[ "$(sys_sum)" = "$sys_before" ]'

# --- 7. host wrapper: exit status and guaranteed rollback --------------------------
HO=$FF802_T1_OUT
run_wrap() { bash "$WRAP" "$@" > "$T/wrap.txt" 2>&1; echo $?; }

rc=$(run_wrap)
ok "wrapper: passing T1 gives exit 0" '[ "$rc" = 0 ] && grep -qx "T1 PASSED" "$HO/log.txt"'
ok "wrapper: results and state files kept" \
    '[ -f "$HO/results/dump-split.txt" ] && [ -f "$HO/results/manifest.txt" ] && [ -f "$HO/state-before.txt" ] && [ -f "$HO/state-after.txt" ]'
ok "wrapper: working dir removed, state identical" '[ ! -e "$W" ] && grep -qx "IDENTICAL" "$HO/log.txt"'

FAKE_MUTATE='s/Channels 22/Channels 2/'; export FAKE_MUTATE
rc=$(run_wrap)
unset FAKE_MUTATE
ok "wrapper: failing T1 gives exit 1" '[ "$rc" = 1 ] && grep -qx "T1 FAILED" "$HO/log.txt"'
ok "wrapper: clean still executed after failing T1" \
    '[ ! -e "$W" ] && grep -q "clean exit: 0" "$HO/log.txt" && grep -q "rollback=verified" "$HO/log.txt"'

cp "$SYS/USB-Audio/USB-Audio.conf" "$T/usb-audio.bak"
echo "Syntax 8" > "$SYS/USB-Audio/USB-Audio.conf"
rc=$(run_wrap)
cp "$T/usb-audio.bak" "$SYS/USB-Audio/USB-Audio.conf"
ok "wrapper: failing prepare gives exit 2, rollback verified" \
    '[ "$rc" = 2 ] && [ ! -e "$W" ] && grep -q "run=skipped" "$HO/log.txt"'

rm -f "$PROC/card1/usbid"
rc=$(run_wrap)
make_proc "Fireface 802 (24047128)" "RME Fireface 802 (24047128)"
ok "wrapper: no card gives exit 3" '[ "$rc" = 3 ] && [ ! -e "$W" ]'

mkdir -p "$W"; echo keep > "$W/foreign.txt"
rc=$(run_wrap)
ok "wrapper: unremovable working dir gives exit 4 and is not touched" \
    '[ "$rc" = 4 ] && [ -f "$W/foreign.txt" ] && grep -q "ROLLBACK NOT VERIFIED" "$HO/log.txt"'
rm -rf -- "${W:?}"

# --- 8. host wrapper: lock against concurrent runs, Ctrl+C -------------------------
if command -v flock >/dev/null 2>&1; then
    flock -o "$WRAP" sleep 30 &
    locker=$!
    sleep 0.5
    rc=$(run_wrap)
    kill "$locker" 2>/dev/null; wait "$locker" 2>/dev/null
    ok "wrapper: concurrent run refused (exit 5), nothing done" \
        '[ "$rc" = 5 ] && [ ! -e "$W" ] && grep -q "another run is active" "$T/wrap.txt"'
else
    echo "skip wrapper lock test (flock not available)"
fi

# Ctrl+C sends SIGINT to the whole foreground process group. Background
# jobs of a non-interactive shell start with SIGINT ignored, so the default
# disposition is restored with env --default-signal.
if command -v setsid >/dev/null 2>&1 && env --default-signal=INT true 2>/dev/null; then
    printf '#!/bin/sh\nsleep 5\nexec "%s" "$@"\n' "$FAKE" > "$T/slow-alsaucm"
    chmod +x "$T/slow-alsaucm"
    rm -f "$T/leader"
    FF802_T1_ALSAUCM=$T/slow-alsaucm setsid env --default-signal=INT \
        sh -c 'echo $$ > "$1/leader"; exec bash "$2" > "$1/wrap-int.txt" 2>&1' sh "$T" "$WRAP" &
    job=$!
    i=0
    while [ $i -lt 100 ] && ! grep -q "=== run" "$HO/log.txt" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
    sleep 0.5
    [ -s "$T/leader" ] && bash -c 'kill -INT -- "-$1"' sh "$(cat "$T/leader")"
    wait "$job"
    rc=$?
    ok "wrapper: Ctrl+C during run gives exit 130" \
        '[ "$rc" = 130 ] && grep -qx "T1 INTERRUPTED (no result)" "$HO/log.txt" && grep -q "run=interrupted" "$HO/log.txt"'
    ok "wrapper: clean executed after Ctrl+C, rollback verified" \
        '[ ! -e "$W" ] && grep -q "clean exit: 0" "$HO/log.txt" && grep -q "rollback=verified" "$HO/log.txt"'
else
    echo "skip Ctrl+C test (setsid or env --default-signal not available)"
fi

ok "system tree unchanged before the package layout tests" '[ "$(sys_sum)" = "$sys_before" ]'

# --- 9. package layout (prepare --package) -------------------------------------------
DEB=$(sh "$SRC/packaging/build-deb.sh" "$T/deb")
LN="RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed"
make_proc "Fireface 802 (24047128)" "RME Fireface 802 (24047128)"
rc=$(run_t1 prepare --package "$DEB")
ok "prepare --package succeeds" '[ "$rc" = 0 ]'
ok "package layout: master file under the card long name" \
    'cmp -s "$W/ucm2/conf.d/USB-Audio/$LN.conf" "$SRC/packaging/ff802-ucm/longname-master.conf"'
ok "package layout: USB-Audio.conf identical to the system file" \
    'cmp -s "$W/ucm2/USB-Audio/USB-Audio.conf" "$SYS/USB-Audio/USB-Audio.conf" && ! grep -q "If.ff802-t1" "$W/ucm2/USB-Audio/USB-Audio.conf"'
ok "package layout: profile files from the package" \
    'cmp -s "$W/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf" "$SRC/audio/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf"'
ok "package layout recorded in the manifest" \
    'grep -q "^layout: package ff802-ucm-profile 0.3.0" "$W/manifest.txt" && grep -q "^binding: conf.d/USB-Audio/$LN.conf" "$W/manifest.txt"'
: > "$LOG"
rc=$(run_t1 run)
ok "run with the package layout passes" '[ "$rc" = 0 ] && grep -q "all checks passed" "$T/out.txt"'
ok "system tree unchanged by the package layout" '[ "$(sys_sum)" = "$sys_before" ]'
rc=$(run_t1 clean)
ok "clean removes the package layout" '[ "$rc" = 0 ] && [ ! -e "$W" ]'

rm -f "$PROC/card1/usbid"
rc=$(run_t1 prepare --package "$DEB")
ok "prepare --package without card: exit 3" '[ "$rc" = 3 ]'
make_proc "Fireface 802 (24047128)" "RME Fireface 802 (24047128)"
rm -rf -- "${W:?}"
rc=$(run_t1 prepare --package "$T/missing.deb")
ok "prepare --package with missing file: exit 2" '[ "$rc" = 2 ] && grep -q "package not found" "$T/out.txt"'
rc=$(run_t1 prepare --package)
ok "prepare --package without file: usage, exit 2" '[ "$rc" = 2 ]'
rm -rf -- "${W:?}"

rc=$(run_wrap --package "$DEB")
ok "wrapper --package: passes, rollback verified" \
    '[ "$rc" = 0 ] && grep -q "package layout: $DEB" "$HO/log.txt" && grep -q "rollback=verified" "$HO/log.txt" && [ ! -e "$W" ]'
rc=$(run_wrap --package)
ok "wrapper --package without file: exit 2" '[ "$rc" = 2 ]'
rc=$(run_wrap --bogus)
ok "wrapper rejects unknown arguments" '[ "$rc" = 2 ]'

ok "system tree unchanged at the end" '[ "$(sys_sum)" = "$sys_before" ]'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
