#!/bin/sh
# Static checks of the T4 udev rule and systemd units (no udev, no
# systemd manager needed; systemd-analyze works offline on copies).
#
# Usage: check_auto_units.sh <source dir> <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
T=$2
P=$SRC/packaging/ff802-cc-auto
RULE=$P/70-ff802-cc-auto.rules
SVC=$P/ff802-cc-auto@.service
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
    */units-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"

rules=$(grep -v '^[[:space:]]*#' "$RULE" | grep -v '^[[:space:]]*$')

# --- udev rule -------------------------------------------------------------
ok "udev rule executes nothing (no RUN, PROGRAM, IMPORT)" \
    '! printf "%s\n" "$rules" | grep -Eq "RUN|PROGRAM|IMPORT"'
ok "udev rule changes no access rights (no MODE, OWNER, GROUP, uaccess)" \
    '! printf "%s\n" "$rules" | grep -Eq "MODE|OWNER|GROUP|uaccess|seat"'
ok "udev rule matches exactly the two Fireface 802 USB IDs" \
    '[ "$(printf "%s\n" "$rules" | grep -o "ATTR{idVendor}==\"[0-9a-f]*\", ATTR{idProduct}==\"[0-9a-f]*\"" | tr "\n" ";")" = \
       "ATTR{idVendor}==\"2a39\", ATTR{idProduct}==\"3fcd\";ATTR{idVendor}==\"0424\", ATTR{idProduct}==\"3fdd\";" ]'
ok "udev rule: proprietary mode wants the enable unit per port, only on add" \
    '[ "$(printf "%s\n" "$rules" | grep -c SYSTEMD_WANTS)" = 2 ] &&
     [ "$(printf "%s\n" "$rules" | sed -n "/^LABEL=\"ff802_cc_auto_prop\"$/,/^LABEL=/p" | grep SYSTEMD_WANTS)" = "ACTION==\"add\", ENV{SYSTEMD_WANTS}+=\"ff802-cc-auto-enable@%k.service\"" ] &&
     printf "%s\n" "$rules" | grep -qx "ATTR{idVendor}==\"2a39\", ATTR{idProduct}==\"3fcd\", GOTO=\"ff802_cc_auto_prop\""'
ok "udev rule: class-compliant mode wants ff802-cc-auto@ per connection (kernel name, busnum, devnum)" \
    '[ "$(printf "%s\n" "$rules" | sed -n "/^LABEL=\"ff802_cc_auto_cc\"$/,/^LABEL=/p" | grep SYSTEMD_WANTS)" = "ACTION==\"add\", ENV{SYSTEMD_WANTS}+=\"ff802-cc-auto@%k:\$env{BUSNUM}:\$env{DEVNUM}.service\"" ] &&
     printf "%s\n" "$rules" | grep -qx "ATTR{idVendor}==\"0424\", ATTR{idProduct}==\"3fdd\", GOTO=\"ff802_cc_auto_cc\""'
ok "udev rule only applies to USB devices (not interfaces)" \
    'printf "%s\n" "$rules" | head -n 2 | tr "\n" ";" | grep -qx "SUBSYSTEM!=\"usb\", GOTO=\"ff802_cc_auto_end\";ENV{DEVTYPE}!=\"usb_device\", GOTO=\"ff802_cc_auto_end\";"'

# --- system services ----------------------------------------------------------
ESVC=$P/ff802-cc-auto-enable@.service
key() { grep -E "^$1=" "$SVC" | sed "s/^$1=//"; }
ekey() { grep -E "^$1=" "$ESVC" | sed "s/^$1=//"; }
# the hardening lines both units must share, unchanged
HARDEN="CapabilityBoundingSet AmbientCapabilities NoNewPrivileges DevicePolicy
ProtectSystem ProtectHome PrivateTmp PrivateNetwork IPAddressDeny RestrictAddressFamilies
ProtectKernelTunables ProtectKernelModules ProtectKernelLogs ProtectControlGroups
ProtectClock ProtectHostname RestrictNamespaces RestrictRealtime RestrictSUIDSGID
LockPersonality MemoryDenyWriteExecute SystemCallArchitectures SystemCallFilter"
hard() { for k in $HARDEN; do grep -E "^$k=" "$1"; done; }
for u in "$SVC" "$ESVC"; do
    n=${u##*/}
    ok "$n: oneshot, one ExecStart with the instance, no other Exec*, no Restart" \
        '[ "$(grep -E "^Type=" "$u")" = Type=oneshot ] &&
         [ "$(grep -E "^ExecStart=" "$u")" = "ExecStart=/usr/libexec/ff802-cc/ff802-cc-autoswitch --service %i" ] &&
         [ "$(grep -cE "^Exec" "$u")" = 1 ] && ! grep -qE "^Restart" "$u"'
    ok "$n: no capabilities, no new privileges, no network, kernel tunables read-only" \
        'grep -qx "CapabilityBoundingSet=" "$u" && grep -qx "AmbientCapabilities=" "$u" &&
         grep -qx "NoNewPrivileges=yes" "$u" && grep -qx "DevicePolicy=closed" "$u" &&
         grep -qx "PrivateNetwork=yes" "$u" && grep -qx "IPAddressDeny=any" "$u" &&
         grep -qx "ProtectSystem=strict" "$u" && grep -qx "ProtectHome=yes" "$u" &&
         grep -qx "ProtectKernelTunables=yes" "$u" && grep -qx "ProtectKernelModules=yes" "$u" &&
         grep -qx "SystemCallFilter=~@privileged" "$u" &&
         ! grep -qE "^(User|Group|DynamicUser|PermissionsStartOnly|ExecStartPre|ExecStartPost|BindPaths|BindReadOnlyPaths|InaccessiblePaths|PrivateDevices|ReadOnlyPaths)=" "$u"'
    ok "$n: bounded run time (60 s), runtime directory /run/ff802-cc-auto kept across instances" \
        'grep -qx "TimeoutStartSec=60" "$u" && grep -qx "RuntimeDirectory=ff802-cc-auto" "$u" &&
         grep -qx "RuntimeDirectoryPreserve=yes" "$u"'
done
ok "both services share exactly the same hardening settings" \
    '[ "$(hard "$SVC")" = "$(hard "$ESVC")" ]'
ok "ff802-cc-auto@: only ALSA device nodes, no USB device nodes, no writable paths" \
    '[ "$(key DeviceAllow | tr "\n" ";")" = "char-alsa rw;" ] && ! grep -qE "^ReadWritePaths=" "$SVC"'
ok "ff802-cc-auto-enable@: only USB device nodes, no ALSA" \
    '[ "$(ekey DeviceAllow | tr "\n" ";")" = "char-usb_device rw;" ]'
ok "ff802-cc-auto-enable@: writable is exactly the port disable attribute of the instance's device" \
    '[ "$(ekey ReadWritePaths | tr "\n" ";")" = "-/sys/bus/usb/devices/%i/port/disable;" ]'

# systemd-analyze on copies whose ExecStart exists in the container
sed 's|/usr/libexec/ff802-cc/ff802-cc-autoswitch|/bin/true|' "$SVC" > "$T/ff802-cc-auto@.service"
sed 's|/usr/libexec/ff802-cc/ff802-cc-autoswitch|/bin/true|' "$ESVC" > "$T/ff802-cc-auto-enable@.service"
for inst in "ff802-cc-auto@3-2.4.1.1.3:3:19.service" "ff802-cc-auto-enable@3-2.4.1.1.3.service"; do
    base=${inst%%@*}
    (cd "$T" && systemd-analyze verify --man=no "$T/$inst") > "$T/verify-$base.txt" 2>&1
    ok "systemd-analyze verify: no warning for $inst" \
        '! grep -q "$base@" "$T/verify-$base.txt"'
    [ -s "$T/verify-$base.txt" ] && sed 's/^/    /' "$T/verify-$base.txt"
    (cd "$T" && systemd-analyze security --offline=yes --no-pager "$T/$inst") > "$T/security-$base.txt" 2>&1
    exposure=$(sed -n 's/.*Overall exposure level for .*: \([0-9.]*\) .*/\1/p' "$T/security-$base.txt")
    echo "info systemd-analyze security $inst: exposure $exposure"
    ok "systemd-analyze security $inst: exposure <= 2.0" \
        '[ -n "$exposure" ] && awk -v e="$exposure" "BEGIN { exit !(e <= 2.0) }"'
done

# --- user units of the notification --------------------------------------------
UPATH=$P/ff802-cc-notify.path
USVC=$P/ff802-cc-notify.service
ok "user path unit watches only the two event files and starts only the notification" \
    '[ "$(grep -E "^Path" "$UPATH" | tr "\n" ";")" = "PathChanged=/run/ff802-cc-auto/event;PathChanged=/run/ff802-cc-auto/mixer-event;" ] &&
     grep -qx "Unit=ff802-cc-notify.service" "$UPATH" && grep -qx "WantedBy=graphical-session.target" "$UPATH"'
ok "user service: oneshot notifier, bounded, part of the graphical session" \
    '[ "$(grep -E "^Exec" "$USVC")" = "ExecStart=/usr/libexec/ff802-cc/ff802-cc-notify" ] &&
     grep -qx "Type=oneshot" "$USVC" && grep -qx "TimeoutStartSec=60" "$USVC" &&
     grep -qx "PartOf=graphical-session.target" "$USVC" && ! grep -qE "^Restart" "$USVC"'
ok "notifier has no USB, privilege or system-unit access and writes only below XDG_RUNTIME_DIR" \
    '! grep -Eq "ff802-cc-autoswitch|/dev/bus|/dev/snd|amidi|aconnect|sudo|pkexec|systemctl|udevadm" "$P/ff802-cc-notify" &&
     [ "$(grep -oE "> *\"[^\"]*\"" "$P/ff802-cc-notify" | sort -u | tr "\n" ";")" = "> \"\$1.tmp\";" ] &&
     [ "$(grep -oE "mark_seen \"[^\"]*\"" "$P/ff802-cc-notify" | sort -u)" = "mark_seen \"\$SEEN\"" ] &&
     [ "$(grep -E "^handle " "$P/ff802-cc-notify" | grep -c "\"\$XDG_RUNTIME_DIR/ff802-cc-notify\.")" = 2 ] &&
     [ "$(grep -cE "^handle " "$P/ff802-cc-notify")" = 2 ]'
sed 's|/usr/libexec/ff802-cc/ff802-cc-notify|/bin/true|' "$USVC" > "$T/ff802-cc-notify.service"
cp "$UPATH" "$T/ff802-cc-notify.path"
xdg=$(mktemp -d /tmp/ff802-xdg.XXXXXX)   # short: socket paths of other units
(cd "$T" && XDG_RUNTIME_DIR=$xdg systemd-analyze --user verify --man=no \
    "$T/ff802-cc-notify.path" "$T/ff802-cc-notify.service") > "$T/verify-user.txt" 2>&1
rc=$?
rm -rf -- "$xdg"
ok "systemd-analyze --user verify: no warning for the notification units" \
    '[ "$rc" = 0 ] && ! grep -q "ff802-cc-notify" "$T/verify-user.txt"'
grep "ff802-cc-notify" "$T/verify-user.txt" | sed 's/^/    /'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
