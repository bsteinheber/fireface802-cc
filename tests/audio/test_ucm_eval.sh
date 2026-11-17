#!/bin/sh
# Evaluation of the UCM profile with the real alsa-lib, without a sound card.
#
# Runs the unchanged host test script tools/hw-test/ff802-ucm-t1.sh in the
# container: a fake control device (libfake_ctl_ff802.so) presents a
# Fireface 802 in CC mode to alsa-lib, ucm_query stands in for alsaucm, and
# a fake /proc/asound lists the card. alsa-lib evaluates the complete UCM
# tree of the system (USB-Audio.conf, conditions, includes, macros,
# SplitPCM) exactly as on the host, up to the point where the card
# information is read.
#
# Everything happens below the scratch directory. ALSA_CONFIG_PATH and
# ALSA_CONFIG_UCM2 are set for the processes of this test only. The system
# UCM tree is only read.
#
# Usage: test_ucm_eval.sh <source dir> <dir with ucm_query and plugin>
#                         <system ucm2 dir> <alsa-lib version> <scratch dir>

set -u

SRC=$1
BIN=$2
SYS=$3
ALSA_VER=$4
T=$5
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
    */eval-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"

# --- private ALSA configuration: hw control devices are the fake card ----
cat > "$T/alsa.conf" <<EOF
</usr/share/alsa/alsa.conf>
ctl_type.ff802fake.lib "$BIN/libfake_ctl_ff802.so"
ctl.!hw {
	@args [ CARD ]
	@args.CARD { type string }
	type ff802fake
	card \$CARD
}
EOF

# --- fake /proc/asound with the card as hw:1 ---------------------------------
PROC=$T/proc-asound
mkdir -p "$PROC/card0" "$PROC/card1"
echo "0424:3fdd" > "$PROC/card1/usbid"
cat > "$PROC/cards" <<'EOF'
 0 [sofsoundwire   ]: sof-soundwire - sof-soundwire
                      DellInc.-XPS179730-0C4XC6
 1 [F80224047128   ]: USB-Audio - Fireface 802 (24047128)
                      RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed
EOF

W=$T/work
ALSA_CONFIG_PATH=$T/alsa.conf
FF802_T1_DIR=$W
FF802_T1_SYSTEM_UCM2=$SYS
FF802_T1_PROC_ASOUND=$PROC
FF802_T1_ALSAUCM=$BIN/ucm_query
export ALSA_CONFIG_PATH FF802_T1_DIR FF802_T1_SYSTEM_UCM2 FF802_T1_PROC_ASOUND FF802_T1_ALSAUCM
unset ALSA_CONFIG_UCM2 FF802_T1_REPO_UCM2 FF802_FAKE_LONGNAME FF802_FAKE_NAME \
      FF802_FAKE_ID FF802_FAKE_COMPONENTS

sys_sum() { (cd "$SYS" && find . -print | sort && find . -type f -exec cksum {} + | sort); }
sys_before=$(sys_sum)

q() { # q <ucm tree> <output file> <ucm_query args...>
    tree=$1; out=$2; shift 2
    ALSA_CONFIG_UCM2=$tree "$BIN/ucm_query" "$@" > "$T/$out" 2>&1
    echo "exit status: $?" >> "$T/$out"
}

# --- 1. T1 prepare and run against the real alsa-lib --------------------------
sh "$SCRIPT" prepare > "$T/prepare.txt" 2>&1
rc=$?
ok "T1 prepare succeeds" '[ "$rc" = 0 ]'
ok "loader inserted once, before If.inc" \
    '[ "$(grep -c "^If\.ff802-t1 {" "$W/ucm2/USB-Audio/USB-Audio.conf")" = 1 ] &&
     awk "/^If\.ff802-t1 \{/{a=NR} /^If\.inc[ \t]*\{/{b=NR} END{exit !(a && b && a < b)}" "$W/ucm2/USB-Audio/USB-Audio.conf"'
ok "overlay differs from the system tree only in USB-Audio.conf and the profile" \
    '[ "$(diff -rq --no-dereference "$SYS" "$W/ucm2" | sort | tr "\n" " ")" = "Files $SYS/USB-Audio/USB-Audio.conf and $W/ucm2/USB-Audio/USB-Audio.conf differ Only in $W/ucm2/USB-Audio/RME: Fireface-802-HiFi.conf Only in $W/ucm2/USB-Audio/RME: Fireface-802.conf Only in $W/ucm2/USB-Audio: conf.d " ]'

sh "$SCRIPT" run > "$T/run.txt" 2>&1
rc=$?
ok "T1 run passes with the real alsa-lib (exit 0)" '[ "$rc" = 0 ]'
ok "T1 run reports no FAIL" '! grep -q "FAIL" "$T/run.txt" && grep -q "all checks passed" "$T/run.txt"'
ok "T1 run evaluated 7 devices" '[ "$(grep -c "SplitPCM gives hw PCM" "$T/run.txt")" = 7 ]'
[ "$rc" = 0 ] || sed 's/^/    /' "$T/run.txt"
mkdir -p "$T/results" && cp "$W/results/"*.txt "$T/results/"
for f in verbs devices dump dump-split baseline-verbs; do
    ok "fixture tests/audio/fixtures/t1/$f.txt matches the real output" \
        'grep -v "^exit status:" "$T/results/$f.txt" | sed "s/^ucm_query: /alsaucm: /" | cmp -s - "$SRC/tests/audio/fixtures/t1/$f.txt"'
done

# --- 2. the alsa-lib optional include problem ------------------------------------
# Same overlay but with the unmodified system USB-Audio.conf: only the conf.d
# hook ("Optional true") could load the profile.
P=$T/plain-ucm2
cp -RP "$W/ucm2" "$P"
cp "$SYS/USB-Audio/USB-Audio.conf" "$P/USB-Audio/USB-Audio.conf"
q "$P" plain.txt -c hw:1 list _verbs
case $ALSA_VER in
    1.2.1[0-5]|1.2.1[0-5].*|1.2.[0-9]|1.2.[0-9].*)
        ok "alsa-lib $ALSA_VER: conf.d hook alone does not load the profile (loader needed)" \
            'grep -q "UCM is not supported" "$T/plain.txt"' ;;
    *)
        ok "alsa-lib $ALSA_VER: conf.d hook alone loads the profile" \
            'grep -qx "  0: HiFi" "$T/plain.txt"' ;;
esac

# --- 3. guards evaluated by the real parser --------------------------------------
FF802_FAKE_LONGNAME="RME Fireface 802 FS (12345678) at usb-0000:00:14.0-2, high speed"
export FF802_FAKE_LONGNAME
q "$W/ucm2" fs.txt -c hw:1 list _verbs
ok "802 FS long name: profile not selected" 'grep -q "UCM is not supported" "$T/fs.txt"'
FF802_FAKE_LONGNAME="RME Fireface UCX (23596862) at usb-0000:00:14.0-2, high speed"
FF802_FAKE_COMPONENTS="USB0424:3fb9"
export FF802_FAKE_COMPONENTS
q "$W/ucm2" ucx.txt -c hw:1 dump text
ok "other RME device (UCX id): our profile not selected" \
    '! grep -q "ff802_stereo\|Headphones Output 9/10" "$T/ucx.txt"'
unset FF802_FAKE_LONGNAME FF802_FAKE_COMPONENTS

# --- 4. generated alsa-lib configuration -----------------------------------------
q "$W/ucm2" alib.txt -c hw:1 alibcfg
ok "_alibcfg parses and defines the dshare/dsnoop split PCMs" \
    'grep -qx "exit status: 0" "$T/alib.txt" &&
     grep -q "^pcm.ff802_stereo_out" "$T/alib.txt" && grep -q "type dshare" "$T/alib.txt" &&
     grep -q "^pcm.ff802_stereo_in" "$T/alib.txt" && grep -q "type dsnoop" "$T/alib.txt"'
ok "_alibcfg: 22 channel positions per split PCM" \
    '[ "$(grep -Ec "^[[:space:]]+F[LR]\$" "$T/alib.txt")" = 44 ]'
q "$W/ucm2" alib-split.txt -c "<<<SplitPCM=1>>>hw:1" alibcfg
ok "_alibcfg with SplitPCM: no split PCM definitions (application splits)" \
    '! grep -q "ff802_stereo" "$T/alib-split.txt"'

# --- 5. read-only tool and rollback ----------------------------------------------
q "$W/ucm2" set.txt -c hw:1 set _verb HiFi
ok "ucm_query refuses state-changing commands" 'grep -qx "exit status: 2" "$T/set.txt"'
sh "$SCRIPT" clean > "$T/clean.txt" 2>&1
rc=$?
ok "T1 clean removes the working directory" '[ "$rc" = 0 ] && [ ! -e "$W" ]'
ok "system UCM tree unchanged" '[ "$(sys_sum)" = "$sys_before" ]'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
