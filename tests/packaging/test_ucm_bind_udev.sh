#!/bin/sh
# ff802-ucm-bind udev <N> (T3 0.3.0): automatic binding on every USB port.
# Private root (DPKG_ROOT), fake /proc/asound, logger stub; nothing
# outside the scratch directory is touched.
#
# Usage: test_ucm_bind_udev.sh <source dir> <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
T=$2
BIND=$SRC/packaging/ff802-ucm/ff802-ucm-bind
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
    */bind-udev-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T/bin"
R=$T/root
CONFD=$R/usr/share/alsa/ucm2/conf.d/USB-Audio
STATE=$R/var/lib/ff802-ucm
TARGET=../../../../ff802-ucm/longname-master.conf
PORTA="RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed"
PORTB="RME Fireface 802 (24047128) at usb-0000:00:14.0-1, high speed"
PORTC="RME Fireface 802 (24047128) at usb-0000:3a:00.0-2.1, high speed"
printf '#!/bin/sh\necho "logger $*" >> "%s/logger.log"\n' "$T" > "$T/bin/logger"
chmod +x "$T/bin/logger"
PATH=$T/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
unset FF802_UCM_ALSA_LIB_VERSION DPKG_ROOT FF802_UCM_PROC_ASOUND

reset() {
    rm -rf -- "${R:?}"
    mkdir -p "$CONFD" "$R/usr/share/ff802-ucm" "$R/run"
    echo "# Ubuntu USB-Audio.conf" > "$CONFD/USB-Audio.conf"
    echo "# master" > "$R/usr/share/ff802-ucm/longname-master.conf"
    rm -f "$T/logger.log"
}
proc() { # proc <dir> <card n> <usbid> <long name>
    mkdir -p "$1/card$2"
    echo "$3" > "$1/card$2/usbid"
    printf ' 0 [sofsoundwire   ]: sof-soundwire - sof-soundwire\n                      DellInc.-XPS179730-0C4XC6\n' > "$1/cards"
    printf ' %s [F80224047128   ]: USB-Audio - Fireface 802 (24047128)\n                      %s\n' "$2" "$4" >> "$1/cards"
}
hook() { # hook <proc dir> <card n> [<alsa-lib version>]: as the udev rule runs it
    env DPKG_ROOT="$R" FF802_UCM_PROC_ASOUND="$1" \
        FF802_UCM_ALSA_LIB_VERSION="${3:-1.2.15.3-1ubuntu1.1}" HOME=/nonexistent \
        sh "$BIND" udev "$2" >> "$T/out.log" 2>&1
    echo $?
}
ours() { [ -L "$CONFD/$1.conf" ] && [ "$(readlink "$CONFD/$1.conf")" = "$TARGET" ]; }
nlinks() { find "$CONFD" -name '*.conf' -type l | wc -l; }
# the tree without run/ (the lock file of the hook lives there)
tree() { (cd "$R" && find . -path ./run -prune -o -printf '%y %p %l\n' | sort &&
         find . -path ./run -prune -o -type f -exec cksum {} + | sort); }

proc "$T/p-a" 1 0424:3fdd "$PORTA"
proc "$T/p-b" 1 0424:3fdd "$PORTB"
proc "$T/p-c" 2 0424:3fdd "$PORTC"

# --- new ports, existing bindings ----------------------------------------------
reset
rc=$(hook "$T/p-a" 1)
ok "first port: binding created as the relative link to the master file, recorded" \
    '[ "$rc" = 0 ] && ours "$PORTA" && [ "$(cat "$STATE/bindings")" = "$PORTA" ] &&
     grep -q "^logger -t ff802-ucm-bind -- udev card1: bound: $PORTA" "$T/logger.log"'
rc=$(hook "$T/p-b" 1)
ok "another (unknown) USB port: second binding, the first one is kept" \
    '[ "$rc" = 0 ] && ours "$PORTA" && ours "$PORTB" && [ "$(nlinks)" = 2 ] &&
     [ "$(cat "$STATE/bindings" | tr "\n" ";")" = "$PORTA;$PORTB;" ]'
rc=$(hook "$T/p-c" 2)
ok "third port on another controller, card number 2" '[ "$rc" = 0 ] && ours "$PORTC" && [ "$(nlinks)" = 3 ]'

before=$(tree)
for i in 1 2 3 4 5; do hook "$T/p-a" 1 > /dev/null; hook "$T/p-b" 1 > /dev/null; done
ok "repeated events: nothing changes (links, record, master), exit 0" \
    '[ "$(tree)" = "$before" ] && [ "$(grep -c "already bound" "$T/logger.log")" = 10 ]'

reset
pids=""
for i in 1 2 3 4 5 6 7 8 9 10; do (hook "$T/p-a" 1 > "$T/rc.$i") & pids="$pids $!"; done
for p in $pids; do wait "$p"; done
ok "ten simultaneous events: one link, one record entry, all exit 0" \
    '[ "$(nlinks)" = 1 ] && ours "$PORTA" && [ "$(grep -cxF "$PORTA" "$STATE/bindings")" = 1 ] &&
     [ "$(cat "$T"/rc.* | sort -u)" = 0 ]'

# --- foreign files are never touched ----------------------------------------------
reset
echo "# someone else's file" > "$CONFD/$PORTA.conf"
ln -s /somewhere/else.conf "$CONFD/$PORTB.conf"
before=$(tree)
rc=$(hook "$T/p-a" 1); rc2=$(hook "$T/p-b" 1)
ok "foreign file and foreign link under the card's name: left alone, not recorded, exit 0" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(tree)" = "$before" ] &&
     [ "$(grep -c "was not created by ff802-ucm-bind; left alone" "$T/logger.log")" = 2 ]'
ok "Ubuntu's USB-Audio.conf unchanged" '[ "$(cat "$CONFD/USB-Audio.conf")" = "# Ubuntu USB-Audio.conf" ]'

# --- only the original Fireface 802 at high speed -------------------------------
n=0
for c in "0424:3fdd|RME Fireface 802 FS (12345678) at usb-0000:00:14.0-1, high speed" \
         "2a39:3fcd|RME Fireface 802 (24047128) at usb-0000:00:14.0-1, high speed" \
         "0424:2514|RME Fireface 802 (24047128) at usb-0000:00:14.0-1, high speed" \
         "0424:3fdd|Microchip USB Audio at usb-0000:00:14.0-1, high speed" \
         "0424:3fdd|RME Fireface UFX+ (1) at usb-0000:00:14.0-1, high speed" \
         "0424:3fdd|RME Fireface 802 (24047128) at usb-0000:00:14.0-1, full speed" \
         "0424:3fdd|RME Fireface 802 (24047128) at usb-../../../etc-1, high speed" \
         "0424:3fdd|RME Fireface 802 (24047128) at usb-0000:00:14.0-1/x, high speed" \
         "0424:3fdd|RME Fireface 802 (2404x128) at usb-0000:00:14.0-1, high speed" \
         "0424:3fdd|.RME Fireface 802 (24047128) at usb-0000:00:14.0-1, high speed" \
         "0424:3fdd|"; do
    n=$((n + 1))
    reset
    proc "$T/p-x$n" 1 "${c%%|*}" "${c#*|}"
    before=$(tree)
    rc=$(hook "$T/p-x$n" 1)
    ok "refused, nothing changed: ${c#*|} (${c%%|*})" \
        '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ] && [ ! -e "$STATE" ]'
done

for a in "" "x" "01" "32" "-1" "1 2" "../1"; do
    reset; before=$(tree)
    rc=$(hook "$T/p-a" "$a")
    ok "invalid card number '$a': nothing done, exit 0" '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ]'
done
reset; before=$(tree)
rc=$(hook "$T/p-a" 3)
ok "card number of another card (no such card): nothing done" '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ]'
env DPKG_ROOT="$R" sh "$BIND" udev > /dev/null 2>&1; rc=$?
ok "udev without a card number: usage error, nothing done" '[ "$rc" = 2 ] && [ "$(tree)" = "$before" ]'

# --- alsa-lib >= 1.2.16: no new binding ---------------------------------------------
for v in 1.2.16 1.2.16-1 1.2.17-0ubuntu1 1.3.0; do
    reset; before=$(tree)
    rc=$(hook "$T/p-a" 1 "$v")
    ok "alsa-lib $v: no binding created" \
        '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ] && grep -q "loads USB-Audio/conf.d itself; no binding created" "$T/logger.log"'
done
reset
hook "$T/p-a" 1 > /dev/null
before=$(tree)
rc=$(hook "$T/p-b" 1 1.2.16-1)
ok "alsa-lib 1.2.16 after an upgrade: existing bindings stay, no new one" \
    '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ] && ours "$PORTA" && ! [ -e "$CONFD/$PORTB.conf" ]'

# --- fault tolerance ----------------------------------------------------------------
reset; rm -f "$R/usr/share/ff802-ucm/longname-master.conf"; before=$(tree)
rc=$(hook "$T/p-a" 1)
ok "master file missing (package being removed): nothing done, exit 0" '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ]'
reset; rm -rf -- "${CONFD:?}"; before=$(tree)
rc=$(hook "$T/p-a" 1)
ok "conf.d/USB-Audio missing: nothing done, exit 0" '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ]'
if [ "$(id -u)" != 0 ]; then
    reset; chmod 0555 "$CONFD"; before=$(tree)
    rc=$(hook "$T/p-a" 1)
    chmod 0755 "$CONFD"
    ok "conf.d/USB-Audio not writable: nothing done, exit 0" '[ "$rc" = 0 ] && [ "$(tree)" = "$before" ]'
    reset; mkdir -p "$R/var/lib"; chmod 0555 "$R/var/lib"; before=$(tree)
    rc=$(hook "$T/p-a" 1)
    chmod 0755 "$R/var/lib"
    ok "state not writable: no link without a record (unbind/purge can always remove it)" \
        '[ "$rc" = 0 ] && [ "$(nlinks)" = 0 ] && grep -q "cannot record the binding" "$T/logger.log"'
fi
reset; rm -rf -- "${R:?}/run"
rc=$(hook "$T/p-a" 1)
ok "no /run for the lock: still binds" '[ "$rc" = 0 ] && ours "$PORTA"'
reset; mkdir -p "$T/nolog"
for t in sh cat sed grep mkdir touch ln readlink printf dpkg wc env; do
    p=$(command -v "$t") && ln -sf "$p" "$T/nolog/$t"
done
rc=$(env -i PATH="$T/nolog" DPKG_ROOT="$R" FF802_UCM_PROC_ASOUND="$T/p-a" \
     FF802_UCM_ALSA_LIB_VERSION=1.2.15.3 sh "$BIND" udev 1 > /dev/null 2>&1; echo $?)
ok "without logger and flock: still binds, exit 0" '[ "$rc" = 0 ] && ours "$PORTA"'

reset
start=$(date +%s%N)
hook "$T/p-a" 1 > /dev/null
ms=$(( ($(date +%s%N) - start) / 1000000 ))
echo "info hook run time: $ms ms"
ok "short: one run takes well below a second (no waiting)" '[ "$ms" -lt 1000 ]'

# --- udev rule (static) ---------------------------------------------------------------
RULE=$SRC/packaging/ff802-ucm/77-ff802-ucm-bind.rules
rules=$(grep -v '^[[:space:]]*#' "$RULE" | grep -v '^[[:space:]]*$')
ok "rule: only the change event of sound card devices card<N> (not controlC, pcm, add)" \
    '[ "$(printf "%s\n" "$rules" | head -n 3 | tr "\n" ";")" = "ACTION!=\"change\", GOTO=\"ff802_ucm_bind_end\";SUBSYSTEM!=\"sound\", GOTO=\"ff802_ucm_bind_end\";KERNEL!=\"card[0-9]*\", GOTO=\"ff802_ucm_bind_end\";" ]'
ok "rule: USB id 0424:3fdd and product Fireface 802* of the same parent, card index %n" \
    'printf "%s\n" "$rules" | grep -qxF "ATTRS{idVendor}==\"0424\", ATTRS{idProduct}==\"3fdd\", ATTRS{product}==\"Fireface 802*\", RUN+=\"/usr/sbin/ff802-ucm-bind udev %n\""'
ok "rule: exactly one RUN, no PROGRAM/IMPORT, no access rights, no environment changes" \
    '[ "$(printf "%s\n" "$rules" | grep -c "RUN")" = 1 ] &&
     ! printf "%s\n" "$rules" | grep -Eq "PROGRAM|IMPORT|MODE|OWNER|GROUP|uaccess|TAG|ENV\{|SYSTEMD_WANTS|OPTIONS"'
ok "rule: numbered before systemd's 78-sound-card.rules, ends with its label" \
    '[ "${RULE##*/}" = "77-ff802-ucm-bind.rules" ] && [ "$(printf "%s\n" "$rules" | tail -n 1)" = "LABEL=\"ff802_ucm_bind_end\"" ]'

# --- static properties ---------------------------------------------------------------
body=$(sed -n '/^cmd_udev() {/,/^}/p' "$BIND")
ok "udev mode never removes, overwrites or forces anything" \
    '! printf "%s\n" "$body" | grep -Eq "rm |ln -s?f|ln -fs|mv |unlink|> *\"\\\$f\""'
ok "udev mode never touches WirePlumber, user sessions, services or udev" \
    '! printf "%s\n" "$body" | grep -Eq "wireplumber|systemctl|udevadm|HOME|XDG_|\.local|pkexec|sudo"'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
