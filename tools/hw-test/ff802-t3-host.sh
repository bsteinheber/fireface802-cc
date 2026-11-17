#!/bin/bash
# ff802-t3-host.sh - controlled host test T3: PipeWire/WirePlumber/Plasma
# integration of the Fireface 802 (package ff802-ucm-profile).
#
# Run on the Kubuntu host as normal user (not root), one step at a time:
#
#   precheck          read only: versions, card, services, nothing installed;
#                     records the state before (incl. a copy of the
#                     WirePlumber state) for the rollback
#   install <deb>     installs the package with sudo apt (asks first)
#   check             commissioning (read only): six sinks, one source,
#                     split positions, SAFETY of the volumes (now and
#                     stored, <= 40 %), default device as set by
#                     set-default, other devices unchanged, comparison
#                     with the first successful check (persistence)
#   verify            normal operation (read only): package installed,
#                     card in CC mode, six sinks, one source, split
#                     positions, persistence as in check; volumes and the
#                     default sink are the user's choice (reported, the
#                     default must exist), no SAFETY limit, no precheck
#                     needed
#   limit             lowers every Fireface sink above 40 % to 40 %
#                     (no other device is touched)
#   set-default       wpctl set-default "Headphones Output 9/10"
#   rollback          removes the package (sudo apt purge, asks first),
#                     restores the WirePlumber state recorded by precheck
#                     and verifies the state before
#   status            shows what this run has done so far
#
# The script never plays or records audio. Before ANY audio output,
# 'check' must pass: it fails with exit 6 (SAFETY) if a Fireface sink is
# above 40 % (= 0.064 linear, -23.9 dB) or a higher volume is stored for
# the Fireface in the WirePlumber state (it would be restored on the next
# reconnect). 'verify' is NOT a safety check: it accepts any volume.
#
# Output (git-ignored): build-t3-host/ (log.txt, before/, checks/, ...)
#
# Exit status:
#   0  step passed
#   1  checks failed
#   2  usage/environment error, or step refused (nothing changed)
#   3  no (or more than one) original Fireface 802 in CC mode found
#   4  rollback NOT verified - inspect build-t3-host/rollback/
#   5  another run of this script is active (nothing was done)
#   6  SAFETY: volume above the limit - no audio output; run 'limit'
#
# Overrides for the container test (never needed on the host):
#   FF802_T3_OUT, FF802_T3_PROC_ASOUND, FF802_T3_UCM2, FF802_T3_SUDO,
#   FF802_T3_APT, FF802_T3_SYSTEMCTL, FF802_T3_DPKG_QUERY,
#   FF802_T3_CONFIRM (answers the confirmation prompt)

set -u
export LC_ALL=C

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=$(cd -- "$SCRIPT_DIR/../.." && pwd)
OUT=${FF802_T3_OUT:-$REPO/build-t3-host}
PROC=${FF802_T3_PROC_ASOUND:-/proc/asound}
UCM2=${FF802_T3_UCM2:-/usr/share/alsa/ucm2}
SUDO=${FF802_T3_SUDO:-sudo}
APT=${FF802_T3_APT:-apt-get}
SYSTEMCTL=${FF802_T3_SYSTEMCTL:-systemctl}
DPKG_QUERY=${FF802_T3_DPKG_QUERY:-dpkg-query}
WPSTATE=${XDG_STATE_HOME:-$HOME/.local/state}/wireplumber
PKG=ff802-ucm-profile
USBID=0424:3fdd
COMPONENTS=USB0424:3fdd
# 40 % on the Pulse/Plasma (cubic) scale = 0.4^3 = 0.064 linear = -23.9 dB,
# the WirePlumber default for new sinks
LIMIT_RAW=26215          # pactl raw volume (65536 = 100 %)
LIMIT_LINEAR=0.064001    # WirePlumber state (linear)

# Expected devices: profile device | split position | description suffix
EXPECTED_SINKS="Line1|[AUX0,AUX1]|Analog Output 1/2
Line2|[AUX2,AUX3]|Analog Output 3/4
Line3|[AUX4,AUX5]|Analog Output 5/6
Line4|[AUX6,AUX7]|Analog Output 7/8
Headphones1|[AUX8,AUX9]|Headphones Output 9/10
Headphones2|[AUX10,AUX11]|Headphones Output 11/12"
EXPECTED_SOURCE="Line5|[AUX0,AUX1]|Analog Input 1/2"

die() { echo "ff802-t3-host: $*" >&2; exit "${2:-2}"; }
info() { echo "ff802-t3-host: $*"; }
fails=0
pass() { echo "ok   $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

usage() { sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

[ "$(id -u)" = 0 ] && die "do not run as root (sudo is used where needed)"
mkdir -p "$OUT" || die "cannot create $OUT"
if command -v flock > /dev/null 2>&1; then
    exec 9< "$0"
    flock -n 9 || die "another run is active; nothing was done" 5
fi

log() { # log <step>: copy all output of the step to log.txt
    exec > >(tee -a "$OUT/log.txt") 2>&1
    echo "=== $(date -u '+%Y-%m-%dT%H:%M:%SZ') $1"
}

confirm() { # confirm <word> <question>
    echo "$2"
    printf 'Type "%s" to continue: ' "$1"
    if [ -n "${FF802_T3_CONFIRM:-}" ]; then
        answer=$FF802_T3_CONFIRM
        echo "$answer (FF802_T3_CONFIRM)"
    else
        read -r answer || answer=""
    fi
    [ "$answer" = "$1" ] || die "not confirmed; nothing was changed"
}

# --- facts -------------------------------------------------------------------
pkg_version() { "$DPKG_QUERY" -W -f='${Status} ${Version}\n' "$1" 2>/dev/null |
                sed -n 's/^install ok installed //p'; }

find_card() { # prints "<index>\t<long name>" of the original Fireface 802
    n=0
    for d in "$PROC"/card[0-9]*; do
        [ "$(cat "$d/usbid" 2>/dev/null)" = "$USBID" ] || continue
        idx=${d##*card}
        ln=$(sed -n "/^ *$idx \[/{n;s/^ *//;s/ *\$//;p;q;}" "$PROC/cards" 2>/dev/null)
        case $ln in *"Fireface 802 ("*) ;; *) continue ;; esac
        case $ln in *"802 FS"*) continue ;; esac
        n=$((n + 1))
        found=$(printf '%s\t%s' "$idx" "$ln")
    done
    [ "$n" = 1 ] || return 1
    printf '%s\n' "$found"
}

# pactl list sinks|sources -> name|description|components|profile|split|maxraw|mute|object.id
pa_nodes() {
    local listing
    listing=$(pactl list "$1" 2>/dev/null) || die "pactl list $1 failed (is pipewire-pulse running?)"
    printf '%s\n' "$listing" | awk '
        function out() { if (name != "") print name "|" desc "|" comp "|" prof "|" spos "|" maxv "|" mute "|" oid }
        /^[A-Z][a-z]+ #[0-9]+$/ { out(); name = desc = comp = prof = spos = mute = oid = ""; maxv = 0 }
        /^\tName: / { name = substr($0, index($0, ":") + 2) }
        /^\tDescription: / { desc = substr($0, index($0, ":") + 2) }
        /^\tMute: / { mute = $2 }
        /^\tVolume: / { for (i = 1; i <= NF; i++) if ($(i + 1) == "/" && $i ~ /^[0-9]+$/ && $i + 0 > maxv) maxv = $i + 0 }
        /^\t\talsa.components = / { comp = $0; sub(/^[^"]*"/, "", comp); sub(/"$/, "", comp) }
        /^\t\tdevice.profile.name = / { prof = $0; sub(/^[^"]*"/, "", prof); sub(/"$/, "", prof) }
        /^\t\tapi.alsa.split.position = / { spos = $0; sub(/^[^"]*"/, "", spos); sub(/"$/, "", spos) }
        /^\t\tobject.id = / { oid = $0; sub(/^[^"]*"/, "", oid); sub(/"$/, "", oid) }
        END { out() }' || die "cannot parse 'pactl list $1'"
}
require_pulse() { # the Pulse API of PipeWire answers and lists at least one device
    pactl info > /dev/null 2>&1 || die "pactl cannot reach PipeWire (pipewire-pulse running?)"
    [ -n "$(pa_nodes sinks)$(pa_nodes sources)" ] || die "pactl lists no audio device at all; cannot check"
}
ff_sinks() { pa_nodes sinks | awk -F'|' -v c="$COMPONENTS" '$3 == c'; }
ff_sources() { pa_nodes sources | awk -F'|' -v c="$COMPONENTS" '$3 == c && $1 !~ /\.monitor$/'; }
other_nodes() { # devices that are not the Fireface: name|maxraw|mute
    { pa_nodes sinks; pa_nodes sources; } | awk -F'|' -v c="$COMPONENTS" '$3 != c { print $1 "|" $6 "|" $7 }' | sort
}
sink_by_dev() { ff_sinks | awk -F'|' -v p="HiFi: $1: sink" '$4 == p'; }
ff_card_name() { # WirePlumber device name of the Fireface (key prefix in its state)
    pactl list cards 2>/dev/null | awk -v c="$COMPONENTS" '
        /^Card #[0-9]+$/ { name = "" }
        /^\tName: / { name = $2 }
        /^\t\talsa.components = / { v = $0; sub(/^[^"]*"/, "", v); sub(/"$/, "", v)
                                     if (v == c && name != "") { print name; exit } }'
}
stored_high() { # stored_high <card name>: stored output routes above the limit
    [ -n "$1" ] && [ -f "$WPSTATE/default-routes" ] || return 0
    awk -v p="$1:output:" 'index($0, p) == 1' "$WPSTATE/default-routes" |
        awk -v lim="$LIMIT_LINEAR" '{
            s = $0; sub(/.*"channelVolumes":\[/, "", s); sub(/\].*/, "", s)
            n = split(s, v, ","); for (i = 1; i <= n; i++) if (v[i] + 0 > lim) { print $0; break } }'
}

state_snapshot() { # files T3 could conceivably affect (read only)
    {
        echo "--- packages"
        for p in $PKG alsa-ucm-conf libasound2t64 pipewire wireplumber pipewire-pulse; do
            echo "$p $(pkg_version "$p")"
        done
        echo "--- conf.d/USB-Audio (bindings)"
        ls -la "$UCM2/conf.d/USB-Audio" 2>&1 | sed 1d | awk '{ $6 = $7 = $8 = ""; print }'
        echo "--- profile files in the UCM tree"
        for f in USB-Audio/conf.d/0424-3fdd.conf USB-Audio/RME/Fireface-802.conf \
                 USB-Audio/RME/Fireface-802-HiFi.conf; do
            [ -e "$UCM2/$f" ] && echo "PRESENT $f" || echo "ABSENT $f"
        done
        [ -e /var/lib/ff802-ucm ] && echo "PRESENT /var/lib/ff802-ucm" || echo "ABSENT /var/lib/ff802-ucm"
        echo "--- configuration (checksums)"
        for p in /etc/pipewire /etc/wireplumber /etc/asound.conf /etc/alsa/conf.d \
                 "$HOME/.asoundrc" "$HOME/.config/pipewire" "$HOME/.config/wireplumber" \
                 "$HOME/.config/environment.d"; do
            if [ -e "$p" ]; then
                find "$p" \( -type f -o -type l \) -print0 2>/dev/null | sort -z |
                    xargs -0 -r sha256sum 2>/dev/null
            else
                echo "ABSENT $p"
            fi
        done
    }
}
wpstate_sum() { (cd "$WPSTATE" 2>/dev/null && find . -type f -print0 | sort -z | xargs -0 -r sha256sum); }

# --- steps -------------------------------------------------------------------
cmd_precheck() {
    log precheck
    require_pulse
    [ -e "$OUT/before" ] && die "$OUT/before exists: a run is in progress (rollback first, or remove $OUT)"
    echo "--- environment"
    grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release
    uname -r
    pw=$(pactl info 2>/dev/null | sed -n 's/^Server Name: .*on PipeWire \([0-9.]*\).*/\1/p')
    wp=$(wireplumber --version 2>/dev/null | sed -n 's/^Linked with libwireplumber \(.*\)$/\1/p')
    echo "PipeWire (server) $pw, WirePlumber $wp"
    echo "alsa-ucm-conf $(pkg_version alsa-ucm-conf), libasound2t64 $(pkg_version libasound2t64)"
    case $pw in 1.6.*) pass "PipeWire 1.6.x" ;; *) fail "PipeWire 1.6.x expected, found '$pw'" ;; esac
    case $wp in 0.5.*) pass "WirePlumber 0.5.x" ;; *) fail "WirePlumber 0.5.x expected, found '$wp'" ;; esac
    case $(pkg_version libasound2t64) in 1.2.15*) pass "alsa-lib 1.2.15.x" ;;
        *) fail "alsa-lib 1.2.15.x expected (the binding is tested for it)" ;; esac
    for s in pipewire wireplumber pipewire-pulse; do
        if [ "$($SYSTEMCTL --user is-active "$s" 2>/dev/null)" = active ]; then pass "service $s active"
        else fail "service $s not active"; fi
    done
    [ -z "$(pkg_version $PKG)" ] && pass "$PKG not installed" || fail "$PKG is already installed"
    card=$(find_card) || { echo "no or more than one original Fireface 802 (USB $USBID) in $PROC/cards"; exit 3; }
    longname=${card#*$'\t'}
    pass "Fireface 802 in CC mode: card ${card%%$'\t'*}, '$longname'"
    if [ -e "$UCM2/conf.d/USB-Audio/$longname.conf" ]; then
        fail "a binding for this card exists already: $UCM2/conf.d/USB-Audio/$longname.conf"
    else
        pass "no binding for this card"
    fi
    n=$(ff_sinks | grep -c 'HiFi: ' || true)
    [ "$n" = 0 ] && pass "no Fireface split sinks yet (UCM profile not active)" ||
        fail "$n Fireface split sinks exist already"
    [ "$fails" = 0 ] || { echo "precheck FAILED ($fails); nothing was recorded"; exit 1; }

    mkdir -p "$OUT/before"
    state_snapshot > "$OUT/before/state.txt"
    other_nodes > "$OUT/before/other-devices.txt"
    pactl get-default-sink > "$OUT/before/default-sink.txt" 2>/dev/null
    pactl get-default-source > "$OUT/before/default-source.txt" 2>/dev/null
    pactl list cards > "$OUT/before/cards.txt" 2>/dev/null
    pactl list sinks > "$OUT/before/sinks-full.txt" 2>/dev/null   # incl. priority.session
    printf '%s\n' "$longname" > "$OUT/before/longname.txt"
    if [ -d "$WPSTATE" ]; then
        cp -a "$WPSTATE" "$OUT/before/wireplumber-state"
    else
        echo ABSENT > "$OUT/before/wireplumber-state.absent"
    fi
    wpstate_sum > "$OUT/before/wireplumber-state.sum"
    echo "--- recorded: $OUT/before (other devices: $(wc -l < "$OUT/before/other-devices.txt"))"
    echo "precheck PASSED. Next: $0 install <path to ${PKG}_<version>_all.deb>"
}

cmd_install() {
    deb=${1:-}
    log "install $deb"
    [ -d "$OUT/before" ] || die "run 'precheck' first"
    [ -f "$deb" ] || die "package file not found: $deb"
    deb=$(cd -- "$(dirname -- "$deb")" && pwd)/$(basename -- "$deb")
    [ "$(dpkg-deb -f "$deb" Package 2>/dev/null)" = "$PKG" ] || die "$deb is not $PKG"
    bad=$(dpkg-deb -c "$deb" | awk '{ print $6 }' | sed 's#^\./##' | grep -v '/$' |
          grep -vE '^usr/share/alsa/ucm2/USB-Audio/(conf\.d/0424-3fdd|RME/Fireface-802(-HiFi)?)\.conf$|^usr/share/ff802-ucm/longname-master\.conf$|^usr/sbin/ff802-ucm-bind$|^usr/share/doc/ff802-ucm-profile/')
    [ -z "$bad" ] || die "unexpected files in the package: $bad"
    echo "package: $PKG $(dpkg-deb -f "$deb" Version), files only below ucm2/USB-Audio, ff802-ucm, sbin, doc"
    cat <<'EOF'

VOLUME SAFETY before the profile becomes active:
  - headphones off the head, headphone knobs of the Fireface fully down
  - amplifiers/active speakers off or at minimum
  - all audio playback stopped
New sinks start at 40 % (-23.9 dB). Running streams may move to
"Headphones Output 9/10" when the Fireface reappears.
EOF
    confirm install "Install $PKG with 'sudo $APT install'?"
    $SUDO "$APT" install -y --no-install-recommends "$deb" || die "installation failed" 1
    $SUDO ff802-ucm-bind status
    cat <<EOF

Installed. Next:
  1. Switch the Fireface off and on (or unplug and replug USB) - keep the
     safety measures above.
  2. $0 check      (must pass before any audio output)
EOF
}

# cmd_check <mode>
#   check   commissioning: SAFETY (<= 40 %, now and stored) and the
#           default set by set-default are required; records the reference
#   verify  normal operation: volumes and the default sink are the user's
#           choice (reported only); everything technical is still required
cmd_check() {
    mode=$1
    log "$mode"
    require_pulse
    if [ "$mode" = check ]; then
        [ -d "$OUT/before" ] || die "run 'precheck' first"
    else
        [ -n "$(pkg_version $PKG)" ] && pass "$PKG installed ($(pkg_version $PKG))" ||
            fail "$PKG is not installed"
        card=$(find_card) || { echo "no or more than one original Fireface 802 (USB $USBID) in $PROC/cards"; exit 3; }
        pass "Fireface 802 in CC mode: card ${card%%$'\t'*}"
    fi
    n=$(ls "$OUT/checks" 2>/dev/null | grep -c "^$mode-" || true)
    C=$OUT/checks/$mode-$(printf '%02d' $((n + 1)))
    mkdir -p "$C"
    ff_sinks > "$C/sinks.txt"
    ff_sources > "$C/sources.txt"
    other_nodes > "$C/other-devices.txt"
    pactl get-default-sink > "$C/default-sink.txt" 2>/dev/null
    safety=0

    echo "--- Fireface sinks (name | description | split | raw volume | mute)"
    awk -F'|' '{ print "  " $1 " | " $2 " | " $5 " | " $6 " | " $7 }' "$C/sinks.txt"
    split_sinks=$(grep -c '|HiFi: [A-Za-z0-9]*: sink|' "$C/sinks.txt" || true)
    [ "$split_sinks" = 6 ] && [ "$(wc -l < "$C/sinks.txt")" = 6 ] &&
        pass "exactly six Fireface sinks, all from the HiFi split profile" ||
        fail "expected six HiFi split sinks, found $split_sinks of $(wc -l < "$C/sinks.txt") Fireface sinks"
    while IFS='|' read -r dev pos suffix; do
        line=$(awk -F'|' -v p="HiFi: $dev: sink" '$4 == p' "$C/sinks.txt")
        if [ -z "$line" ]; then fail "sink $dev missing"; continue; fi
        IFS='|' read -r name desc _ _ split raw mute _ <<< "$line"
        [ "$split" = "$pos" ] && case $desc in *"$suffix") true ;; *) false ;; esac &&
            pass "$dev: $suffix, split $pos" ||
            fail "$dev: expected '$suffix' $pos, found '$desc' $split"
        if [ "$mode" = verify ]; then
            echo "info $dev volume $((raw * 100 / 65536)) %${mute:+ (mute: $mute)} (user setting)"
        elif [ "$raw" -le "$LIMIT_RAW" ]; then
            pass "SAFETY $dev volume $((raw * 100 / 65536)) % <= 40 %${mute:+ (mute: $mute)}"
        else
            fail "SAFETY $dev volume $((raw * 100 / 65536)) % > 40 % ($name)"
            safety=1
        fi
    done <<< "$EXPECTED_SINKS"
    IFS='|' read -r dev pos suffix <<< "$EXPECTED_SOURCE"
    [ "$(wc -l < "$C/sources.txt")" = 1 ] &&
        awk -F'|' -v p="HiFi: $dev: source" -v s="$pos" '$4 == p && $5 == s { f = 1 } END { exit !f }' "$C/sources.txt" &&
        pass "exactly one Fireface source: $suffix, split $pos" ||
        fail "expected one source '$suffix' $pos, found: $(cut -d'|' -f2,5 "$C/sources.txt" | tr '\n' ';')"

    # stored WirePlumber state for the Fireface routes (restored on reconnect)
    card=$(ff_card_name)
    echo "--- Fireface card: ${card:-not found}"
    [ -n "$card" ] || fail "Fireface card not found in PipeWire"
    high=$(stored_high "$card")
    if [ "$mode" = verify ]; then
        [ -z "$high" ] || echo "info stored Fireface route volumes above 40 % (user setting): $high"
    elif [ -n "$high" ]; then
        fail "SAFETY stored Fireface route volume above 40 % (restored on reconnect): $high"
        safety=1
    else
        pass "SAFETY no stored Fireface route volume above 40 %"
    fi

    echo "--- default sink: $(cat "$C/default-sink.txt")"
    if [ "$mode" = verify ]; then
        # any existing sink the user chose; it must exist
        pa_nodes sinks | cut -d'|' -f1 | grep -qxF -- "$(cat "$C/default-sink.txt")" &&
            pass "default sink exists: $(cat "$C/default-sink.txt") (user choice)" ||
            fail "no valid default sink ('$(cat "$C/default-sink.txt")')"
    elif [ -f "$OUT/set-default.done" ]; then
        hp=$(sink_by_dev Headphones1 | cut -d'|' -f1)
        [ "$(cat "$C/default-sink.txt")" = "$hp" ] && pass "default sink is Headphones Output 9/10 (set-default)" ||
            fail "default sink is not Headphones Output 9/10 although set-default was run"
    fi
    if [ "$mode" = check ]; then
        if diff <(cut -d'|' -f1 "$OUT/before/other-devices.txt") <(cut -d'|' -f1 "$C/other-devices.txt") > "$C/other-devices.diff"; then
            pass "other audio devices still present ($(wc -l < "$C/other-devices.txt"))"
        else
            fail "other audio devices changed (see $C/other-devices.diff)"
        fi
        if ! diff -q "$OUT/before/other-devices.txt" "$C/other-devices.txt" > /dev/null; then
            echo "info volumes/mute of other devices changed since precheck (by you or applications):"
            diff "$OUT/before/other-devices.txt" "$C/other-devices.txt" | sed 's/^/    /'
        fi
    else
        # other devices may come and go in normal operation
        echo "info other audio devices: $(cut -d'|' -f1 "$C/other-devices.txt" | grep -vc '\.monitor$' || true) (not required)"
    fi

    # persistence: names, descriptions, split positions as in the first passing check
    cut -d'|' -f1,2,4,5 "$C/sinks.txt" "$C/sources.txt" | sort > "$C/identity.txt"
    if [ -f "$OUT/checks/reference-identity.txt" ]; then
        if diff "$OUT/checks/reference-identity.txt" "$C/identity.txt" > "$C/identity.diff"; then
            pass "PERSISTENT same names, descriptions and split positions as $(cat "$OUT/checks/reference-name.txt")"
        else
            fail "names/descriptions/split positions differ from $(cat "$OUT/checks/reference-name.txt"):"
            sed 's/^/    /' "$C/identity.diff"
        fi
    fi
    if [ "$mode" = check ] && [ "$fails" = 0 ] && [ ! -f "$OUT/checks/reference-identity.txt" ]; then
        cp "$C/identity.txt" "$OUT/checks/reference-identity.txt"
        basename "$C" > "$OUT/checks/reference-name.txt"
        echo "info $(basename "$C") is the reference for the persistence checks"
    fi

    if [ "$safety" = 1 ]; then
        echo "check FAILED: SAFETY - no audio output. Run '$0 limit', then check again."
        exit 6
    fi
    [ "$fails" = 0 ] || { echo "$mode FAILED ($fails)"; exit 1; }
    echo "$mode PASSED ($(basename "$C"))"
    [ "$mode" = verify ] && echo "info normal operation: volumes and default are not limited; for commissioning use '$0 check'"
    true
}

cmd_limit() {
    log limit
    require_pulse
    changed=0
    # wpctl sets the device route (stored by WirePlumber). pactl may set only
    # the node volume while pipewire-pulse does not know the active route
    # (e.g. right after another route change); that is not stored and the
    # stored route volume would come back on the next reconnect.
    while IFS="|" read -r name desc _ _ _ raw _ oid; do
        [ -n "$name" ] || continue
        if [ "$raw" -gt "$LIMIT_RAW" ]; then
            wpctl set-volume "$oid" 0.40 && echo "lowered to 40 %: $desc" && changed=$((changed + 1))
        fi
    done < <(ff_sinks)
    card=$(ff_card_name)
    n=50
    while [ -n "$(stored_high "$card")" ] && [ "$n" -gt 0 ]; do sleep 0.1; n=$((n - 1)); done
    if [ -n "$(stored_high "$card")" ]; then
        echo "limit: stored Fireface volume still above 40 % - run check, do not play audio"
        exit 6
    fi
    echo "limit: $changed Fireface sink(s) lowered, stored state <= 40 %; no other device touched"
}

cmd_set_default() {
    log set-default
    require_pulse
    hp=$(sink_by_dev Headphones1)
    [ -n "$hp" ] || die "sink Headphones Output 9/10 not found (run check)" 1
    id=$(printf '%s\n' "$hp" | cut -d'|' -f8)
    [ -f "$OUT/default-before.txt" ] ||
        { grep -s '^default.configured.audio.sink=' "$WPSTATE/default-nodes" || echo "none"; } > "$OUT/default-before.txt"
    wpctl set-default "$id" || die "wpctl set-default failed" 1
    touch "$OUT/set-default.done"
    # WirePlumber writes its state only after a timeout; a restart before
    # that would lose the configured default (wait as limit does)
    name=${hp%%|*}
    n=50
    until grep -qsxF "default.configured.audio.sink=$name" "$WPSTATE/default-nodes" || [ "$n" = 0 ]; do
        sleep 0.1; n=$((n - 1))
    done
    grep -qsxF "default.configured.audio.sink=$name" "$WPSTATE/default-nodes" ||
        die "the configured default was not stored by WirePlumber ($WPSTATE/default-nodes)" 1
    echo "default sink: $(pactl get-default-sink 2>/dev/null) (configured before: $(cat "$OUT/default-before.txt")), stored"
}

cmd_rollback() {
    log rollback
    [ -d "$OUT/before" ] || die "no state recorded by 'precheck'; nothing to roll back to"
    R=$OUT/rollback
    rm -rf "$R"; mkdir -p "$R"
    confirm rollback "Remove $PKG (sudo $APT purge) and restore the WirePlumber state of precheck (volume/default changes made since then, also for other devices, are undone)? WirePlumber is restarted."
    if [ -n "$(pkg_version $PKG)" ]; then
        $SUDO "$APT" purge -y "$PKG" || echo "purge failed"
    fi
    $SYSTEMCTL --user stop wireplumber
    if [ -d "$OUT/before/wireplumber-state" ]; then
        mkdir -p "$WPSTATE"
        find "$WPSTATE" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
        cp -a "$OUT/before/wireplumber-state/." "$WPSTATE/"
    else
        rm -rf -- "$WPSTATE"
    fi
    wpstate_sum > "$R/wireplumber-state.sum"
    $SYSTEMCTL --user start wireplumber
    sleep 3
    require_pulse

    ok=1
    state_snapshot > "$R/state.txt"
    if diff "$OUT/before/state.txt" "$R/state.txt" > "$R/state.diff"; then
        pass "package, bindings, UCM tree and configuration as before"
    else
        fail "state differs from precheck:"; sed 's/^/    /' "$R/state.diff"; ok=0
    fi
    if diff "$OUT/before/wireplumber-state.sum" "$R/wireplumber-state.sum" > "$R/wireplumber-state.diff"; then
        pass "WirePlumber state restored exactly"
    else
        fail "WirePlumber state not restored:"; sed 's/^/    /' "$R/wireplumber-state.diff"; ok=0
    fi
    n=$(ff_sinks | grep -c 'HiFi: ' || true)
    [ "$n" = 0 ] && pass "no Fireface split sinks any more" || { fail "$n Fireface split sinks remain"; ok=0; }
    other_nodes > "$R/other-devices.txt"
    diff <(cut -d'|' -f1 "$OUT/before/other-devices.txt") <(cut -d'|' -f1 "$R/other-devices.txt") > /dev/null &&
        pass "other audio devices present as before" || { fail "other audio devices differ"; ok=0; }
    echo "info default sink now: $(pactl get-default-sink 2>/dev/null), before: $(cat "$OUT/before/default-sink.txt")"
    echo "info the Fireface gets its previous (non-UCM) profiles after it is reconnected"
    if [ "$ok" = 1 ]; then
        mv "$OUT/before" "$OUT/before.rolled-back-$(date +%Y%m%d%H%M%S)"
        rm -f "$OUT/set-default.done"
        echo "rollback VERIFIED"
    else
        echo "rollback NOT VERIFIED - inspect $R"
        exit 4
    fi
}

cmd_status() {
    echo "output: $OUT"
    [ -d "$OUT/before" ] && echo "precheck: recorded" || echo "precheck: not recorded"
    echo "package: $(pkg_version $PKG || true)"
    ls "$OUT/checks" 2>/dev/null | grep -E '^(check|verify)-' | sed 's/^/run: /'
    [ -f "$OUT/set-default.done" ] && echo "set-default: done (before: $(cat "$OUT/default-before.txt"))"
    true
}

case ${1:-} in
    precheck) [ $# = 1 ] || usage; cmd_precheck ;;
    install) [ $# = 2 ] || usage; cmd_install "$2" ;;
    check) [ $# = 1 ] || usage; cmd_check check ;;
    verify) [ $# = 1 ] || usage; cmd_check verify ;;
    limit) [ $# = 1 ] || usage; cmd_limit ;;
    set-default) [ $# = 1 ] || usage; cmd_set_default ;;
    rollback) [ $# = 1 ] || usage; cmd_rollback ;;
    status) cmd_status ;;
    *) usage ;;
esac
