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
ok "udev rule wants the unit only on add, per connection (kernel name, busnum, devnum)" \
    '[ "$(printf "%s\n" "$rules" | grep -c SYSTEMD_WANTS)" = 1 ] &&
     printf "%s\n" "$rules" | grep -qx "ACTION==\"add\", ENV{SYSTEMD_WANTS}+=\"ff802-cc-auto@%k:\$env{BUSNUM}:\$env{DEVNUM}.service\""'
ok "udev rule only applies to USB devices (not interfaces)" \
    'printf "%s\n" "$rules" | head -n 2 | tr "\n" ";" | grep -qx "SUBSYSTEM!=\"usb\", GOTO=\"ff802_cc_auto_end\";ENV{DEVTYPE}!=\"usb_device\", GOTO=\"ff802_cc_auto_end\";"'

# --- system service -----------------------------------------------------------
key() { grep -E "^$1=" "$SVC" | sed "s/^$1=//"; }
ok "service: oneshot, one ExecStart with the instance, no other Exec*, no Restart" \
    '[ "$(key Type)" = oneshot ] &&
     [ "$(key ExecStart)" = "/usr/libexec/ff802-cc/ff802-cc-autoswitch --service %i" ] &&
     [ "$(grep -cE "^Exec" "$SVC")" = 1 ] && ! grep -qE "^Restart" "$SVC"'
ok "service: no capabilities, no new privileges, only usb_device nodes, no network" \
    '[ "$(key CapabilityBoundingSet)" = "" ] && grep -qx "CapabilityBoundingSet=" "$SVC" &&
     [ "$(key NoNewPrivileges)" = yes ] && [ "$(key DevicePolicy)" = closed ] &&
     [ "$(key DeviceAllow)" = "char-usb_device rw" ] && [ "$(key PrivateNetwork)" = yes ] &&
     [ "$(key ProtectSystem)" = strict ] && [ "$(key ProtectHome)" = yes ]'
ok "service: bounded run time, runtime directory /run/ff802-cc-auto kept across instances" \
    '[ "$(key TimeoutStartSec)" = 30 ] && [ "$(key RuntimeDirectory)" = ff802-cc-auto ] &&
     [ "$(key RuntimeDirectoryPreserve)" = yes ]'

# systemd-analyze on a copy whose ExecStart exists in the container
inst="ff802-cc-auto@3-2.4.1.1.3:3:19.service"
sed 's|/usr/libexec/ff802-cc/ff802-cc-autoswitch|/bin/true|' "$SVC" > "$T/ff802-cc-auto@.service"
(cd "$T" && systemd-analyze verify --man=no "$T/$inst") > "$T/verify.txt" 2>&1
ok "systemd-analyze verify: no warning for the service" \
    '! grep -q "ff802-cc-auto@" "$T/verify.txt"'
[ -s "$T/verify.txt" ] && sed 's/^/    /' "$T/verify.txt"
(cd "$T" && systemd-analyze security --offline=yes --no-pager "$T/$inst") > "$T/security.txt" 2>&1
exposure=$(sed -n 's/.*Overall exposure level for .*: \([0-9.]*\) .*/\1/p' "$T/security.txt")
echo "info systemd-analyze security: exposure $exposure"
ok "systemd-analyze security: exposure <= 2.0" \
    '[ -n "$exposure" ] && awk -v e="$exposure" "BEGIN { exit !(e <= 2.0) }"'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
