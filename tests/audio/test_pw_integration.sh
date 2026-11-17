#!/bin/sh
# PipeWire/WirePlumber integration test (T3) with a fake Fireface 802.
#
# Runs a private PipeWire session (pipewire, wireplumber, pipewire-pulse)
# whose runtime, configuration, state and home directories all lie below
# the scratch directory. The card is the fake Fireface 802:
#
#   fake_spa_enum_ff802  stands in for PipeWire's udev enumerator only
#                        (the container has no /dev/snd); it reports hw:1
#                        while <scratch>/enum/present exists
#   fake_ctl_ff802       control device: card info of the Fireface 802
#   fake_pcm_ff802       hardware PCM: 22 channels S24_3LE; measures the
#                        peak of every playback channel, fixed capture
#                        values per channel
#
# The UCM tree is the system tree with the files of the ff802-ucm-profile
# package installed into a private copy (T1 "prepare --package", the
# layout of the installed package). Everything else is unchanged code of
# the installed packages: WirePlumber's monitors/alsa.lua creates the ACP
# device, the SplitPCM hardware nodes and the loopback sinks; PipeWire
# runs the graph.
#
# Test-only plumbing in the private UCM copy: alsa-lib gives each UCM card
# a private library configuration with its own ctl.hw/pcm.hw ("type hw"),
# which the global configuration cannot reach. A LibraryConfig block at the
# top of the private copy of the binding master file sets ctl_type.hw and
# pcm_type.hw to the fake plugins. The profile files are not changed.
#
# Nothing is played or recorded on real hardware. The system UCM tree is
# only read. No process of the test outlives it.
#
# Usage: test_pw_integration.sh <source dir> <build dir of tests/audio>
#                               <PipeWire root, normally /> <system ucm2>
#                               <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
BIN=$2
ROOT=${3%/}
SYS=$4
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
die() { echo "test_pw_integration: $*" >&2; exit 2; }

case $T in
    */pw-scratch) ;;
    *) die "refusing unexpected scratch dir: $T" ;;
esac
# PipeWire's socket path ($XDG_RUNTIME_DIR/pipewire-0) must fit into 108 bytes
[ ${#T} -le 80 ] || die "scratch dir path too long for the PipeWire socket: $T"
for d in /etc/pipewire /etc/wireplumber; do
    if [ -n "$(find "$d" -type f 2>/dev/null | head -n 1)" ]; then
        die "$d contains configuration that would leak into the private session"
    fi
done
LIB=""
for d in "$ROOT"/usr/lib/x86_64-linux-gnu "$ROOT"/usr/lib/aarch64-linux-gnu; do
    [ -d "$d/spa-0.2" ] && LIB=$d && break
done
[ -n "$LIB" ] || die "no PipeWire SPA plugin directory below $ROOT/usr/lib"

rm -rf -- "${T:?}"
mkdir -p "$T"

# --- private environment ---------------------------------------------------
H=$T/home
mkdir -p "$T/run" "$H" "$T/config/wireplumber/wireplumber.conf.d" "$T/state" \
         "$T/data" "$T/etc" "$T/enum" "$T/pcm" "$T/log"
chmod 700 "$T/run"
PATH=$ROOT/usr/bin:/usr/sbin:/usr/bin:/sbin:/bin
HOME=$H
XDG_RUNTIME_DIR=$T/run
XDG_CONFIG_HOME=$T/config
XDG_STATE_HOME=$T/state
XDG_DATA_HOME=$T/data
XDG_CONFIG_DIRS=$T/etc
XDG_DATA_DIRS=$ROOT/usr/share
PIPEWIRE_CONFIG_DIR=$ROOT/usr/share/pipewire
PIPEWIRE_MODULE_DIR=$LIB/pipewire-0.3
SPA_PLUGIN_DIR=$BIN/spa:$LIB/spa-0.2
WIREPLUMBER_MODULE_DIR=$LIB/wireplumber-0.5
ALSA_CONFIG_PATH=$T/alsa.conf
ALSA_CONFIG_UCM2=$T/t1/ucm2
FF802_FAKE_ENUM_DIR=$T/enum
FF802_FAKE_PCM_DIR=$T/pcm
export PATH HOME XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_STATE_HOME XDG_DATA_HOME \
       XDG_CONFIG_DIRS XDG_DATA_DIRS PIPEWIRE_CONFIG_DIR PIPEWIRE_MODULE_DIR \
       SPA_PLUGIN_DIR WIREPLUMBER_MODULE_DIR ALSA_CONFIG_PATH ALSA_CONFIG_UCM2 \
       FF802_FAKE_ENUM_DIR FF802_FAKE_PCM_DIR
if [ "$ROOT" != "" ]; then   # unpacked package tree instead of /
    LD_LIBRARY_PATH=$LIB:$LIB/pulseaudio
    export LD_LIBRARY_PATH
else
    unset LD_LIBRARY_PATH
fi
unset DBUS_SESSION_BUS_ADDRESS DBUS_SYSTEM_BUS_ADDRESS PULSE_SERVER PULSE_RUNTIME_PATH \
      PIPEWIRE_REMOTE PIPEWIRE_RUNTIME_DIR PIPEWIRE_DEBUG WIREPLUMBER_DEBUG \
      WIREPLUMBER_CONFIG_DIR WIREPLUMBER_DATA_DIR FF802_T1_REPO_UCM2 \
      FF802_FAKE_LONGNAME FF802_FAKE_NAME FF802_FAKE_ID FF802_FAKE_COMPONENTS

sys_sum() { (cd "$SYS" && find . -print | sort && find . -type f -exec cksum {} + | sort); }
sys_before=$(sys_sum)

# --- processes ---------------------------------------------------------------
PIDS=""      # daemons of the running session
ALL_PIDS=""  # every daemon ever started by this test
kill_stub_wp() { # wireplumber started by the systemctl stub (section 9)
    if [ -f "$T/wp-stub.pid" ]; then
        p=$(cat "$T/wp-stub.pid")
        kill "$p" 2>/dev/null
        wait_until 5 '! kill -0 "$p" 2>/dev/null'
        rm -f "$T/wp-stub.pid"
    fi
}
cleanup() {
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    for p in $PIDS; do wait "$p" 2>/dev/null; done
    PIDS=""
    kill_stub_wp
}
trap cleanup EXIT
trap 'exit 3' INT TERM

wait_until() { # wait_until <seconds> <shell condition>
    n=$(( $1 * 10 ))
    while [ "$n" -gt 0 ]; do
        eval "$2" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}

q() { "$@" 2>/dev/null; }   # client tools log D-Bus/RT warnings in the container

daemon() { # daemon <name>: start in the background, log to log/<name>.log
    "$1" >> "$T/log/$1.log" 2>&1 &
    PIDS="$PIDS $!"
    ALL_PIDS="$ALL_PIDS $!"
    [ "$1" = wireplumber ] && echo "$!" > "$T/wp.pid"
}
session_start() {
    daemon pipewire
    wait_until 10 '[ -S "$T/run/pipewire-0" ]' || return 1
    daemon wireplumber
    daemon pipewire-pulse
    wait_until 10 '[ -S "$T/run/pulse/native" ] && q wpctl status > /dev/null' || return 1
    if [ -e "$T/enum/present" ]; then
        wait_until 15 'fireface_sinks | grep -q Headphones1' || return 1
        sleep 1   # routes and default nodes settle
    fi
    return 0
}
session_stop() {
    kill_stub_wp
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    for p in $PIDS; do wait "$p" 2>/dev/null; done
    PIDS=""
    rm -f "$T/run/pipewire-0" "$T/run/pipewire-0-manager" "$T/run/pipewire-0.lock" \
          "$T/run/pipewire-0-manager.lock"
}
session_restart() { session_stop; session_start; }
plug() {
    touch "$T/enum/present"
    wait_until 15 'fireface_sinks | grep -q Headphones1' && sleep 1
}
unplug() {
    rm -f "$T/enum/present"
    wait_until 10 '[ -z "$(fireface_sinks)" ]'
}
# a second audio device, as the internal sound card on the host
add_other_sink() {
    q pw-cli create-node adapter '{ factory.name = support.null-audio-sink
        node.name = ff802-test.other node.description = "Other device"
        media.class = Audio/Sink object.linger = true audio.position = [ FL FR ]
        priority.session = 1000 }' > /dev/null
    wait_until 5 'q pactl list sinks short | grep -q ff802-test.other'
}

# --- queries -----------------------------------------------------------------
FF=usb-RME_Fireface_802_24047128-00
OUT=alsa_output.$FF.HiFi__
IN=alsa_input.$FF.HiFi__
SINKS="Line1 Line2 Line3 Line4 Headphones1 Headphones2"

fireface_sinks() { q pactl list sinks short | awk '{print $2}' | grep "^$OUT" | sort; }
fireface_sources() { q pactl list sources short | awk '{print $2}' | grep "^$IN" | sort; }
node_id() {
    q pw-cli ls Node | awk -v want="$1" '
        /^[[:space:]]*id [0-9]+,/ { id = $2; sub(",", "", id) }
        /node.name = / { n = $0; sub(/.*node.name = "/, "", n); sub(/"$/, "", n)
                         if (n == want) { print id; exit } }'
}
node_prop() { # node_prop <node name> <key>
    id=$(node_id "$1")
    [ -n "$id" ] && q pw-cli info "$id" | sed -n "s/^[* ]*[[:space:]]*$2 = \"\(.*\)\"$/\1/p" | head -n 1
}
volume() { # wpctl volume of a node (cubic scale, as shown by Plasma)
    id=$(node_id "$1")
    [ -n "$id" ] && q wpctl get-volume "$id" | awk '{print $2}'
}
set_volume() { q wpctl set-volume "$(node_id "$1")" "$2"; }
desc() { q pactl list sinks | awk -v want="$1" '
    /^[[:space:]]*Name: / { n = $2 }
    /^[[:space:]]*Description: / && n == want { sub(/^[[:space:]]*Description: /, ""); print; exit }'; }

# --- test signal and peak measurement ------------------------------------------
# full scale square wave, 500 Hz, s16 stereo, 48 kHz
square() { # square <seconds> <file>
    awk -v n="$(( $1 * 48000 ))" 'BEGIN { for (i = 0; i < n; i++)
        printf "%s", (int(i / 48) % 2 == 0) ? "\377\177\377\177" : "\001\200\001\200" }' > "$2"
}
square 1 "$T/sq1.raw"
square 4 "$T/sq4.raw"
FS=8388607
# default sink volume of WirePlumber: 0.064 (linear) = 0.40 (cubic) = -23.9 dB
P_LO=$(( FS * 635 / 10000 ))
P_HI=$(( FS * 645 / 10000 ))
LIMIT=$(( FS * 645 / 10000 ))

peaks_reset() { rm -f "$T/pcm/playback.peaks"; sleep 0.3; }
peak() { awk -v c="$1" '$1 == "ch" && $2 == c { print $4 }' "$T/pcm/playback.peaks" 2>/dev/null; }
play() { # play <sink> <file> [pw-cat options]
    s=$1; f=$2; shift 2
    q pw-cat -p --raw --format s16 --rate 48000 --channels 2 --target "$s" "$@" "$f"
}
# only channels a and b carry the signal, at the default volume
only_pair() { # only_pair <ch a> <ch b> [<lo> <hi>]
    lo=${3:-$P_LO}; hi=${4:-$P_HI}
    awk -v a="$1" -v b="$2" -v lo="$lo" -v hi="$hi" '
        $1 == "ch" { seen++
            if ($2 == a || $2 == b) { if ($4 < lo || $4 > hi) bad++ }
            else if ($4 != 0) bad++ }
        END { exit !(seen == 22 && bad == 0) }' "$T/pcm/playback.peaks" 2>/dev/null
}
max_peak() { awk '$1 == "ch" && $4 > m { m = $4 } END { print m + 0 }' "$T/pcm/playback.peaks" 2>/dev/null; }
# channels 0..11 at the default volume, 12..21 (AES, ADAT) silent
all_analog() {
    awk -v lo="$P_LO" -v hi="$P_HI" '
        $1 == "ch" { seen++
            if ($2 < 12) { if ($4 < lo || $4 > hi) bad++ }
            else if ($4 != 0) bad++ }
        END { exit !(seen == 22 && bad == 0) }' "$T/pcm/playback.peaks" 2>/dev/null
}

# --- 0. setup --------------------------------------------------------------------
pw_ver=$(q pipewire --version | sed -n 's/^Linked with libpipewire \(.*\)$/\1/p')
wp_ver=$(q wireplumber --version | sed -n 's/^Linked with libwireplumber \(.*\)$/\1/p')
echo "info PipeWire $pw_ver, WirePlumber $wp_ver"
ok "PipeWire 1.6.x and WirePlumber 0.5.x (Kubuntu 26.04)" \
    'case "$pw_ver" in 1.6.*) ;; *) false ;; esac && case "$wp_ver" in 0.5.*) ;; *) false ;; esac'

PROC=$T/proc-asound
mkdir -p "$PROC/card0" "$PROC/card1"
echo "0424:3fdd" > "$PROC/card1/usbid"
cat > "$PROC/cards" <<'EOF'
 0 [sofsoundwire   ]: sof-soundwire - sof-soundwire
                      DellInc.-XPS179730-0C4XC6
 1 [F80224047128   ]: USB-Audio - Fireface 802 (24047128)
                      RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed
EOF
DEB=$(sh "$SRC/packaging/build-deb.sh" "$T/deb")
FF802_T1_DIR=$T/t1 FF802_T1_SYSTEM_UCM2=$SYS FF802_T1_PROC_ASOUND=$PROC \
    sh "$SRC/tools/hw-test/ff802-ucm-t1.sh" prepare --package "$DEB" > "$T/log/prepare.txt" 2>&1
rc=$?
MASTER="$T/t1/ucm2/conf.d/USB-Audio/RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed.conf"
ok "UCM tree with the installed package layout prepared (T1 prepare --package)" \
    '[ "$rc" = 0 ] && [ -f "$MASTER" ] && cmp -s "$SYS/USB-Audio/USB-Audio.conf" "$T/t1/ucm2/USB-Audio/USB-Audio.conf"'
{
    echo "# Test only (pw_integration): hw types of the private UCM library"
    echo "# configuration are the fake Fireface 802."
    echo "LibraryConfig.ff802test.Config {"
    echo "	ctl_type.hw { lib \"$BIN/libfake_ctl_ff802.so\" open \"_snd_ctl_ff802fake_open\" }"
    echo "	pcm_type.hw { lib \"$BIN/libfake_pcm_ff802.so\" open \"_snd_pcm_ff802fake_open\" }"
    echo "}"
    cat "$MASTER"
} > "$MASTER.new" && mv "$MASTER.new" "$MASTER"
cat > "$T/alsa.conf" <<EOF
</usr/share/alsa/alsa.conf>
ctl_type.hw { lib "$BIN/libfake_ctl_ff802.so" open "_snd_ctl_ff802fake_open" }
pcm_type.hw { lib "$BIN/libfake_pcm_ff802.so" open "_snd_pcm_ff802fake_open" }
EOF
# WirePlumber: only the udev enumerator is replaced; the remaining SPA
# library mappings are taken from the installed wireplumber.conf
{
    echo "# Test only: the fake enumerator replaces PipeWire's udev enumerator."
    echo "override.context.spa-libs = {"
    echo "  api.alsa.enum.udev = ff802-test/libspa-ff802-fake-enum"
    awk '/^context\.spa-libs = \{/ { on = 1; next } on && /^\}/ { exit } on' \
        "$ROOT/usr/share/wireplumber/wireplumber.conf"
    echo "}"
    echo "wireplumber.profiles = { main = { hardware.bluetooth = disabled, hardware.video-capture = disabled } }"
} > "$T/config/wireplumber/wireplumber.conf.d/90-ff802-container-test.conf"

touch "$T/enum/present"
session_start
rc=$?
ok "private PipeWire session with WirePlumber and pipewire-pulse starts" '[ "$rc" = 0 ]'
[ "$rc" = 0 ] || { tail -n 20 "$T/log/wireplumber.log" | sed 's/^/    /'; exit 1; }

# --- 1. devices as Plasma sees them (Pulse API) ----------------------------------
ok "exactly six Fireface sinks: Line1..4, Headphones1..2" \
    '[ "$(fireface_sinks | tr "\n" " ")" = "${OUT}Headphones1__sink ${OUT}Headphones2__sink ${OUT}Line1__sink ${OUT}Line2__sink ${OUT}Line3__sink ${OUT}Line4__sink " ]'
ok "exactly one Fireface source: Line5 (Analog Input 1/2)" \
    '[ "$(fireface_sources | tr "\n" " ")" = "${IN}Line5__source " ]'
ok "the 22 channel SplitPCM hardware nodes are not offered to applications" \
    '! q pactl list sinks short | grep -q "hw_F80224047128" &&
     ! q pactl list sources short | grep -q "hw_F80224047128" &&
     [ "$(node_prop alsa_output.hw_F80224047128_0 media.class)" = "Audio/Sink/Internal" ] &&
     [ "$(node_prop alsa_input.hw_F80224047128_0 media.class)" = "Audio/Source/Internal" ]'
ok "descriptions name the outputs" \
    '[ "$(desc ${OUT}Line1__sink)" = "Fireface 802 (24047128) Analog Output 1/2" ] &&
     [ "$(desc ${OUT}Line4__sink)" = "Fireface 802 (24047128) Analog Output 7/8" ] &&
     [ "$(desc ${OUT}Headphones1__sink)" = "Fireface 802 (24047128) Headphones Output 9/10" ] &&
     [ "$(desc ${OUT}Headphones2__sink)" = "Fireface 802 (24047128) Headphones Output 11/12" ]'
ok "split positions as in the profile (UCM SplitPCM via ACP)" \
    '[ "$(node_prop ${OUT}Line1__sink api.alsa.split.position)" = "[AUX0,AUX1]" ] &&
     [ "$(node_prop ${OUT}Line2__sink api.alsa.split.position)" = "[AUX2,AUX3]" ] &&
     [ "$(node_prop ${OUT}Line3__sink api.alsa.split.position)" = "[AUX4,AUX5]" ] &&
     [ "$(node_prop ${OUT}Line4__sink api.alsa.split.position)" = "[AUX6,AUX7]" ] &&
     [ "$(node_prop ${OUT}Headphones1__sink api.alsa.split.position)" = "[AUX8,AUX9]" ] &&
     [ "$(node_prop ${OUT}Headphones2__sink api.alsa.split.position)" = "[AUX10,AUX11]" ] &&
     [ "$(node_prop ${IN}Line5__source api.alsa.split.position)" = "[AUX0,AUX1]" ]'
ok "default sink Headphones1, default source Line5 (no configured default)" \
    '[ "$(q pactl get-default-sink)" = "${OUT}Headphones1__sink" ] &&
     [ "$(q pactl get-default-source)" = "${IN}Line5__source" ]'

# --- 2. initial volume ---------------------------------------------------------
vols=""
for s in $SINKS; do vols="$vols$(volume "${OUT}${s}__sink") "; done
echo "info initial sink volumes (cubic): $vols"
ok "all six sinks are created at 0.40 (cubic) = 0.064 linear = -23.9 dB" \
    '[ "$vols" = "0.40 0.40 0.40 0.40 0.40 0.40 " ]'
ok "no route volume stored yet (default applied, not restored state)" \
    '! grep -qs "channelVolumes" "$T/state/wireplumber/default-routes"'
ok "SplitPCM hardware nodes at unity (no extra gain behind the sinks)" \
    '[ "$(volume alsa_output.hw_F80224047128_0)" = "1.00" ]'

# --- 3. signal paths at the hardware PCM -----------------------------------------

for s in $SINKS; do
    case $s in
        Line1) a=0 ;; Line2) a=2 ;; Line3) a=4 ;; Line4) a=6 ;;
        Headphones1) a=8 ;; Headphones2) a=10 ;;
    esac
    peaks_reset
    play "${OUT}${s}__sink" "$T/sq1.raw"
    sleep 0.3
    ok "sink $s reaches hardware channels $a/$((a + 1)) only, at the default volume" \
        'only_pair $a $((a + 1))'
done
peaks_reset
players=""
for s in $SINKS; do
    play "${OUT}${s}__sink" "$T/sq4.raw" &
    players="$players $!"
done
for p in $players; do wait "$p"; done
sleep 0.3
ok "all six sinks play at the same time (channels 0..11 carry signal, 12..21 silent)" 'all_analog'
q pw-record --raw --format s32 --rate 48000 --channels 2 --target "${IN}Line5__source" "$T/rec.raw" &
rec=$!
sleep 1.5
kill "$rec" 2>/dev/null; wait "$rec" 2>/dev/null
ok "Line5 delivers hardware capture channels 0/1 (Analog Input 1/2)" \
    '[ "$(od -An -v -td4 -w8 "$T/rec.raw" | awk "\$1 != 0 { print \$1, \$2 }" | sort -u)" = "16777216 33554432" ]'

# --- 4. card appears while an application plays ----------------------------------
# (fresh WirePlumber state each time; the other device stands in for the
# internal sound card)
race_ok=0
for i in 1 2 3; do
    session_stop
    rm -rf "$T/state/wireplumber"
    rm -f "$T/enum/present"
    session_start && add_other_sink || continue
    peaks_reset
    q pw-cat -p --raw --format s16 --rate 48000 --channels 2 \
        -P '{ application.name = "ff802-race" }' "$T/sq4.raw" & player=$!
    sleep 1
    plug
    wait "$player"
    sleep 0.3
    mp=$(max_peak)
    echo "info plug-in during playback, run $i: default $(q pactl get-default-sink), max peak $mp (limit $LIMIT)"
    if [ "$(q pactl get-default-sink)" = "${OUT}Headphones1__sink" ] && only_pair 8 9 1 "$LIMIT"; then
        race_ok=$((race_ok + 1))
    fi
done
ok "card appearing during playback: stream moves to Headphones1, never above -23.9 dB (3 runs)" \
    '[ "$race_ok" = 3 ]'

# --- 5. persistence: volume, restart, replug ---------------------------------------
set_volume "${OUT}Headphones1__sink" 0.25
ok "a changed volume is stored by WirePlumber (default-routes)" \
    'wait_until 10 "grep -q \"Headphones1=.*channelVolumes.:.0.015625\" \"$T/state/wireplumber/default-routes\""'
session_restart
ok "after a restart of PipeWire/WirePlumber: Headphones1 0.25, the others 0.40" \
    '[ "$(volume ${OUT}Headphones1__sink)" = "0.25" ] && [ "$(volume ${OUT}Line1__sink)" = "0.40" ] &&
     [ "$(volume ${OUT}Headphones2__sink)" = "0.40" ]'
add_other_sink
unplug
ok "after unplugging: no Fireface sink or source, the other device remains default" \
    '[ -z "$(fireface_sinks)$(fireface_sources)" ] && [ "$(q pactl get-default-sink)" = "ff802-test.other" ]'
plug
ok "after replugging: same six sink names, Headphones1 0.25, the others 0.40" \
    '[ "$(fireface_sinks | wc -l)" = 6 ] && [ "$(volume ${OUT}Headphones1__sink)" = "0.25" ] &&
     [ "$(volume ${OUT}Line3__sink)" = "0.40" ]'

# --- 6. persistence: configured default (wpctl set-default) --------------------------
q wpctl set-default "$(node_id ${OUT}Line2__sink)"
ok "wpctl set-default stores the configured default" \
    'wait_until 10 "grep -q \"default.configured.audio.sink=.*Line2__sink\" \"$T/state/wireplumber/default-nodes\""'
session_restart
add_other_sink
ok "configured default kept after a restart" '[ "$(q pactl get-default-sink)" = "${OUT}Line2__sink" ]'
unplug
ok "configured default absent: falls back to the other device (not displaced)" \
    '[ "$(q pactl get-default-sink)" = "ff802-test.other" ]'
plug
ok "configured default back after replugging" '[ "$(q pactl get-default-sink)" = "${OUT}Line2__sink" ]'
q wpctl set-default "$(node_id ${OUT}Headphones1__sink)"

# --- 7. output per application ------------------------------------------------------
pa_play() { # pa_play <client name> <file>: like a Plasma/Pulse application
    q pacat --playback --raw --format=s16le --rate=48000 --channels=2 \
        --client-name="$1" --stream-name=ff802-test < "$2"
}
pa_play ff802-app-a "$T/sq4.raw" & player=$!
wait_until 5 'q pactl list sink-inputs short | grep -q .'
idx=$(q pactl list sink-inputs short | awk 'NR == 1 { print $1 }')
q pactl move-sink-input "$idx" "${OUT}Line3__sink"
wait "$player"
ok "moving an application stores its output (WirePlumber stream state)" \
    'wait_until 10 "grep -q \"application.name:ff802-app-a=.*Line3__sink\" \"$T/state/wireplumber/stream-properties\""'
session_restart
peaks_reset
pa_play ff802-app-a "$T/sq1.raw"
sleep 0.3
ok "after a restart the application plays on Line3 again (channels 4/5)" 'only_pair 4 5'
peaks_reset
pa_play ff802-app-b "$T/sq1.raw"
sleep 0.3
# Headphones1 is at 0.25 (cubic) = 0.015625 linear since section 5
ok "another application uses the default (Headphones1, channels 8/9, at its stored 0.25)" \
    'only_pair 8 9 $(( FS * 154 / 10000 )) $(( FS * 159 / 10000 ))'

# --- 8. stored state overrides the default volume ------------------------------------
set_volume "${OUT}Line4__sink" 1.0
wait_until 10 'grep -q "Line4=.*channelVolumes.:.1.0" "$T/state/wireplumber/default-routes"'
unplug
plug
ok "a stored full volume is restored on replug (state wins over the default; host test must check it)" \
    '[ "$(volume ${OUT}Line4__sink)" = "1.00" ]'

# --- 9. host test script ff802-t3-host.sh against this session ---------------------
# Real pactl/wpctl output of the private session; sudo, apt-get, systemctl
# and dpkg-query (for the package only) are logging stubs. The apt-get stub
# "purge" removes the binding from the private UCM copy, as the postrm does.
ST=$T/stubs
mkdir -p "$ST"
cat > "$ST/sudo" <<EOF
#!/bin/sh
echo "sudo \$*" >> "$T/log/stubs.log"
case \$1 in "$ST/apt-get") exec "\$@" ;; esac
exit 0
EOF
cat > "$ST/apt-get" <<EOF
#!/bin/sh
echo "apt-get \$*" >> "$T/log/stubs.log"
case \$1 in
    install) touch "$T/pkg-installed" ;;
    purge) rm -f "$T/pkg-installed" "\$FF802_TEST_MASTER" ;;
esac
exit 0
EOF
cat > "$ST/dpkg-query" <<EOF
#!/bin/sh
for a in "\$@"; do last=\$a; done
if [ "\$last" = ff802-ucm-profile ]; then
    [ -e "$T/pkg-installed" ] && echo "install ok installed 0.2.0" && exit 0
    exit 1
fi
exec dpkg-query "\$@"
EOF
cat > "$ST/systemctl" <<EOF
#!/bin/sh
echo "systemctl \$*" >> "$T/log/stubs.log"
[ "\$1" = --user ] || exit 1
case "\$2 \$3" in
    "is-active "*) echo active ;;
    "stop wireplumber")
        for f in wp.pid wp-stub.pid; do
            p=\$(cat "$T/\$f" 2>/dev/null) && kill "\$p" 2>/dev/null
        done
        sleep 1 ;;
    "start wireplumber")
        wireplumber >> "$T/log/wireplumber.log" 2>&1 &
        echo \$! > "$T/wp-stub.pid"
        sleep 2 ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$ST"/*
t3() { # t3 <step> [args]: the host script with the overrides of the container
    FF802_T3_OUT=$T/t3-host FF802_T3_PROC_ASOUND=$PROC FF802_T3_UCM2=$SYS \
    FF802_T3_SUDO=$ST/sudo FF802_T3_APT=$ST/apt-get FF802_T3_SYSTEMCTL=$ST/systemctl \
    FF802_T3_DPKG_QUERY=$ST/dpkg-query FF802_TEST_MASTER=$MASTER \
        bash "$SRC/tools/hw-test/ff802-t3-host.sh" "$@" > "$T/log/t3-$1.txt" 2>&1
}
cp "$MASTER" "$T/master.saved"
session_stop
rm -rf "$T/state/wireplumber"
rm -f "$T/enum/present"
session_start && add_other_sink
t3 precheck
rc=$?
ok "host script: precheck passes before installation (card without profile)" '[ "$rc" = 0 ]'
[ "$rc" = 0 ] || sed 's/^/    /' "$T/log/t3-precheck.txt"
FF802_T3_CONFIRM=install t3 install "$DEB"
rc=$?
ok "host script: install asks, then runs sudo apt-get install with the package" \
    '[ "$rc" = 0 ] && grep -q "^apt-get install -y --no-install-recommends $DEB\$" "$T/log/stubs.log"'
plug
t3 check
rc=$?
ok "host script: check passes after the card appears (reference recorded)" \
    '[ "$rc" = 0 ] && grep -q "is the reference" "$T/log/t3-check.txt" &&
     [ "$(grep -c "^ok   SAFETY" "$T/log/t3-check.txt")" = 7 ]'
[ "$rc" = 0 ] || sed 's/^/    /' "$T/log/t3-check.txt"
set_volume "${OUT}Line4__sink" 1.0
wait_until 10 'grep -q "Line4=.*channelVolumes.:.1.0" "$T/state/wireplumber/default-routes"'
t3 check
rc=$?
ok "host script: check refuses with exit 6 (SAFETY) when a sink is above 40 %" \
    '[ "$rc" = 6 ] && grep -q "^FAIL SAFETY Line4 volume 100 %" "$T/log/t3-check.txt" &&
     grep -q "^FAIL SAFETY stored Fireface route volume" "$T/log/t3-check.txt"'
q pactl set-sink-volume ff802-test.other 70%
t3 limit
ok "host script: limit lowers only the Fireface sink above 40 %" \
    '[ "$(volume ${OUT}Line4__sink)" = "0.40" ] && [ "$(volume ff802-test.other)" = "0.70" ] &&
     grep -q "1 Fireface sink(s) lowered, stored state <= 40 %" "$T/log/t3-limit.txt"'
t3 check
rc=$?
ok "host script: check passes again after limit" '[ "$rc" = 0 ]'
t3 set-default
t3 check
rc=$?
ok "host script: set-default makes Headphones 9/10 the default, check confirms it" \
    '[ "$rc" = 0 ] && [ "$(q pactl get-default-sink)" = "${OUT}Headphones1__sink" ] &&
     grep -q "^ok   default sink is Headphones Output 9/10" "$T/log/t3-check.txt"'
session_restart
add_other_sink
t3 check
rc=$?
ok "host script: check after restart reports PERSISTENT" \
    '[ "$rc" = 0 ] && grep -q "^ok   PERSISTENT" "$T/log/t3-check.txt"'
unplug
plug
t3 check
rc=$?
ok "host script: check after replug reports PERSISTENT" \
    '[ "$rc" = 0 ] && grep -q "^ok   PERSISTENT" "$T/log/t3-check.txt"'
FF802_T3_CONFIRM=rollback t3 rollback
rc=$?
[ -f "$T/wp-stub.pid" ] && ALL_PIDS="$ALL_PIDS $(cat "$T/wp-stub.pid")"
ok "host script: rollback purges, restores the WirePlumber state and verifies it" \
    '[ "$rc" = 0 ] && grep -q "^rollback VERIFIED" "$T/log/t3-rollback.txt" &&
     grep -q "^apt-get purge -y ff802-ucm-profile\$" "$T/log/stubs.log"'
[ "$rc" = 0 ] || sed 's/^/    /' "$T/log/t3-rollback.txt"
ok "after rollback: no Fireface split sink, the other device is default again" \
    '[ -z "$(fireface_sinks)" ] && [ "$(q pactl get-default-sink)" = "ff802-test.other" ]'
cp "$T/master.saved" "$MASTER"

# --- 10. cleanup -----------------------------------------------------------------------
session_stop
left=""
for p in $ALL_PIDS; do kill -0 "$p" 2>/dev/null && left="$left $p"; done
ok "no process of the private session is left" '[ -z "$left" ]'
FF802_T1_DIR=$T/t1 sh "$SRC/tools/hw-test/ff802-ucm-t1.sh" clean > /dev/null 2>&1
ok "system UCM tree unchanged" '[ "$(sys_sum)" = "$sys_before" ]'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
