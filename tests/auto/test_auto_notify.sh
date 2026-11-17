#!/bin/sh
# T4: desktop notification (ff802-cc-notify) with test doubles for
# notify-send and busctl, and its decoupling from the system service
# (simulation build of ff802-cc-autoswitch).
#
# Usage: test_auto_notify.sh <source dir> <sim program> <scratch dir>

set -u
LC_ALL=C.UTF-8
export LC_ALL

SRC=$1
SIM=$2
T=$3
NOTIFY=$SRC/packaging/ff802-cc-auto/ff802-cc-notify
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
    */notify-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
R=$T/root
PORT=3-2.4.1.1.3
DEVS=$R/sys/bus/usb/devices
CALLS=$T/calls.log

# --- test doubles -------------------------------------------------------------------
mkdir -p "$T/bin-full" "$T/bin-busctl" "$T/bin-none" "$T/bin-noserver"
cat > "$T/bin-full/notify-send" <<EOF
#!/bin/sh
printf 'notify-send' >> "$CALLS"; for a in "\$@"; do printf ' [%s]' "\$a" >> "$CALLS"; done; echo >> "$CALLS"
EOF
cat > "$T/bin-full/busctl" <<EOF
#!/bin/sh
case "\$*" in "--user status org.freedesktop.Notifications") exit 0 ;; esac
printf 'busctl' >> "$CALLS"; for a in "\$@"; do printf ' [%s]' "\$a" >> "$CALLS"; done; echo >> "$CALLS"
EOF
cp "$T/bin-full/busctl" "$T/bin-busctl/busctl"
cat > "$T/bin-noserver/busctl" <<EOF
#!/bin/sh
echo "status-check" >> "$CALLS"
exit 1
EOF
cp "$T/bin-full/notify-send" "$T/bin-noserver/notify-send"
chmod +x "$T"/bin-*/*
# only basic tools: the real notify-send/busctl of the host are never visible
mkdir -p "$T/sysbin"
for tool in sh cat sed head tr cut date mv sleep env; do
    ln -s "$(command -v "$tool")" "$T/sysbin/$tool"
done
SYSPATH=$T/sysbin
notify() { # notify <bin dir> [env...]: run the notifier like the user unit
    b=$1; shift
    env -i HOME="$T" PATH="$T/$b:$SYSPATH" XDG_RUNTIME_DIR="$T/xdg" \
        FF802_NOTIFY_ROOT="$R" FF802_NOTIFY_WAIT=1 "$@" sh "$NOTIFY" >> "$T/notify.log" 2>&1
    echo $?
}
shown() { n=$(grep -c "^$1" "$CALLS" 2>/dev/null); echo "${n:-0}"; }

reset() {
    rm -rf -- "${R:?}" "$T/xdg" "$CALLS"
    mkdir -p "$DEVS" "$R/run/ff802-cc-auto" "$T/xdg"
}
dev() { # dev <vid> <devnum>
    d=$DEVS/$PORT; mkdir -p "$d"
    echo "$1" > "$d/idVendor"; echo 3 > "$d/busnum"; echo "$2" > "$d/devnum"
}
event() { # event <seq> <kind> <devnum> [<age s>] [<detail>]
    now=$(date +%s)
    printf 'seq=%s\nkind=%s\ntime=%s\nport=%s\nbusnum=3\ndevnum=%s\ndetail=%s\n' \
        "$1" "$2" $((now - ${4:-0})) "$PORT" "$3" "${5:-x}" > "$R/run/ff802-cc-auto/event"
}

# --- notifier -------------------------------------------------------------------------
reset; dev 2a39 18; event 1 prepared 18
rc=$(notify bin-full)
ok "prepared: one critical notification with the proposed text" \
    '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 1 ] &&
     grep -qF "notify-send [--app-name=Fireface 802] [--icon=audio-card] [--urgency=critical] [Fireface 802 – Class-Compliant-Modus vorbereitet] [Bitte die USB-Verbindung des Fireface kurz trennen und wieder anschließen.]" "$CALLS"'
for i in 1 2 3 4 5; do notify bin-full > /dev/null; done
ok "no flood: the same event is shown once per session" '[ "$(shown notify-send)" = 1 ]'

dev 0424 19; event 2 cc-active 19
rc=$(notify bin-full)
ok "cc-active after reconnecting: short confirmation" \
    '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 2 ] &&
     grep -qF "[--urgency=normal] [Fireface 802 – Class-Compliant-Modus aktiv] [Das Fireface ist für den Linux-Audiobetrieb bereit.]" "$CALLS"'

reset; dev 0424 19; event 1 cc-active 19 300
rc=$(notify bin-full)
ok "login long after the event: old confirmation (5 min) not shown" '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 0 ]'

reset; dev 2a39 19; event 1 prepared 18
rc=$(notify bin-full)
ok "prepared, but the device was reconnected meanwhile: not shown" '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 0 ]'
reset; event 1 prepared 18
rc=$(notify bin-full)
ok "prepared, but the device is no longer connected: not shown" '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 0 ]'

reset; dev 2a39 18; event 1 prepared 18 1200
rc=$(notify bin-full)
ok "9: boot without a session, login 20 min later, device still not reconnected: shown at login" \
    '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 1 ]'

reset; dev 2a39 18; event 1 failed 18 0 "transfer failed (bulk transfer timed out, 0 of 4 bytes); device state unknown"
rc=$(notify bin-full)
ok "failed: notification with the reason and where to look" \
    '[ "$rc" = 0 ] && grep -qF "[Fireface 802 – automatische Umschaltung nicht erfolgt] [transfer failed (bulk transfer timed out, 0 of 4 bytes); device state unknown. Details: ff802-cc-auto status, journalctl -t ff802-cc-auto]" "$CALLS"'

reset; dev 2a39 18; event 1 prepared 18
rc=$(notify bin-busctl)
ok "without notify-send: org.freedesktop.Notifications.Notify via busctl, urgency critical" \
    '[ "$rc" = 0 ] && [ "$(shown busctl)" = 1 ] &&
     grep -qF "busctl [--user] [call] [org.freedesktop.Notifications] [/org/freedesktop/Notifications] [org.freedesktop.Notifications] [Notify] [susssasa{sv}i] [Fireface 802] [0] [audio-card] [Fireface 802 – Class-Compliant-Modus vorbereitet]" "$CALLS" &&
     grep -qF "[0] [1] [urgency] [y] [2] [-1]" "$CALLS"'

reset; dev 2a39 18; event 1 prepared 18
rc=$(notify bin-noserver)
rc2=$(notify bin-noserver)
ok "9: no notification service in the session: gives up after the wait, exits 0, no repetition" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(shown notify-send)" = 0 ] &&
     grep -q "no notification service in this session" "$T/notify.log" &&
     [ "$(cat "$T/xdg/ff802-cc-notify.seen")" = 1 ]'

reset; dev 2a39 18; event 1 prepared 18
rc=$(env -i PATH="$T/bin-full:$SYSPATH" FF802_NOTIFY_ROOT="$R" sh "$NOTIFY" 2>&1; echo "rc=$?")
ok "9: no user session (no XDG_RUNTIME_DIR): nothing shown, exit 0" \
    '[ "$(shown notify-send)" = 0 ] && printf "%s" "$rc" | grep -q "rc=0" &&
     printf "%s" "$rc" | grep -q "no XDG_RUNTIME_DIR"'

reset; dev 2a39 18; event 1 prepared 18
rc=$(notify bin-none)
ok "neither notify-send nor busctl: logged, exit 0" \
    '[ "$rc" = 0 ] && grep -q "neither notify-send nor busctl" "$T/notify.log"'

for bad in 'kind=reboot' 'port=../../etc' 'seq=1;x' 'devnum=$(id)'; do
    reset; dev 2a39 18; event 1 prepared 18
    key=${bad%%=*}
    sed -i "s|^$key=.*|$bad|" "$R/run/ff802-cc-auto/event"
    rc=$(notify bin-full)
    ok "invalid event ($bad): ignored" '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 0 ]'
done
reset
rc=$(notify bin-full)
ok "no event file (nothing happened yet): exit 0" '[ "$rc" = 0 ] && [ "$(shown notify-send)" = 0 ]'

# --- decoupling from the system service ---------------------------------------------
reset; mkdir -p "$R/var/lib/ff802-cc-auto"; echo on > "$R/var/lib/ff802-cc-auto/mode"
d=$DEVS/$PORT; mkdir -p "$d"
echo 2a39 > "$d/idVendor"; echo 3fcd > "$d/idProduct"; echo 3 > "$d/busnum"; echo 18 > "$d/devnum"
echo 480 > "$d/speed"; echo RME > "$d/manufacturer"; echo "Fireface 802 (24047128)" > "$d/product"
svc() { env INVOCATION_ID=t FF802_AUTO_ROOT="$R" "$SIM" --service 3-2.4.1.1.3:003:018 >> "$T/svc.log" 2>&1; }
svc
rc=$(notify bin-noserver)
svc; svc
rc2=$(notify bin-full)
writes=$(grep -c '^write ' "$R/usb.log")
ok "a failed notification never leads to another USB write (one write, later events: none)" \
    '[ "$writes" = 1 ] && [ "$rc" = 0 ] && [ "$rc2" = 0 ] && [ "$(shown notify-send)" = 0 ]'
ok "the system service writes the event; the notifier reads it (event file world-readable)" \
    '[ "$(stat -c %a "$R/run/ff802-cc-auto/event")" = 644 ] && grep -q "^kind=prepared" "$R/run/ff802-cc-auto/event"'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
