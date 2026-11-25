#!/bin/sh
# Package test of ff802-cc-auto (T4) in an isolated root (no root rights,
# nothing outside the scratch directory is touched).
#
# - build twice: reproducible; contents, modes, owners, control, md5sums
# - the packaged program is write-capable but is never executed here
# - dpkg --root installs, upgrades, removes and purges with stand-in
#   packages for the dependencies; systemctl, udevadm, pkexec, logger and
#   the program are tripwires in PATH: nothing may call them
# - rollback: after purge the root equals the state before installation
#
# Usage: test_deb_auto.sh <source dir> <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
T=$2
P=$SRC/packaging/ff802-cc-auto
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
    */deb-auto-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
ARCH=$(dpkg --print-architecture)

# --- tripwires: any call is recorded --------------------------------------------
TW=$T/tripwire
mkdir -p "$TW"
for c in systemctl udevadm pkexec logger ff802-cc-autoswitch notify-send busctl sudo; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/calls.log"\nexit 0\n' "$c" "$TW" > "$TW/$c"
    chmod +x "$TW/$c"
done
PATH=$TW:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
calls() { cat "$TW/calls.log" 2>/dev/null; }

# --- build -----------------------------------------------------------------------
DEB=$(sh "$SRC/packaging/build-deb-auto.sh" "$T/deb" 2> "$T/build.err")
DEB2=$(sh "$SRC/packaging/build-deb-auto.sh" "$T/deb2" 2>> "$T/build.err")
ok "package builds" '[ -f "$DEB" ] && [ "${DEB##*/}" = "ff802-cc-auto_0.2.1~dev1_${ARCH}.deb" ]'
[ -f "$DEB" ] || { cat "$T/build.err"; exit 1; }
ok "build is reproducible (identical bytes in another directory)" 'cmp -s "$DEB" "$DEB2"'
ok "19: building the package called no tripwire (nothing executed, no system access)" '[ -z "$(calls)" ]'
ok "19: the build script never executes the program (only compiles, strips, copies it)" \
    '[ "$(grep -n "ff802-cc-autoswitch" "$SRC/packaging/build-deb-auto.sh" | grep -v "^[0-9]*:#" |
          grep -vE "^[0-9]+:cmake --build .* --target ff802-cc-autoswitch |^[0-9]+:put 0755 |^[0-9]+:strip --strip-unneeded |^[0-9]+:nm -D --undefined-only |^[0-9]+:for p in ff802-cc-autoswitch ff802-mixer-init; do$" | wc -l)" = 0 ]'
ok "19: the build script never executes ff802-mixer-init either (only compiles, strips, copies it)" \
    '[ "$(grep -n "ff802-mixer-init" "$SRC/packaging/build-deb-auto.sh" | grep -v "^[0-9]*:#" |
          grep -vE "^[0-9]+:cmake --build .* ff802-mixer-init -j |^[0-9]+:put 0755 |^[0-9]+:strip --strip-unneeded |^[0-9]+:for p in ff802-cc-autoswitch ff802-mixer-init; do$" | wc -l)" = 0 ]'

dpkg-deb -c "$DEB" | awk '{ print $1, $2, $6, $7, $8 }' | sed 's/ *$//' | sort > "$T/contents.txt"
cat > "$T/contents.expected.in" <<'EOF'
drwxr-xr-x root/root ./
drwxr-xr-x root/root ./usr/
drwxr-xr-x root/root ./usr/bin/
-rwxr-xr-x root/root ./usr/bin/ff802-cc-auto
-rwxr-xr-x root/root ./usr/bin/ff802-mixer-init
drwxr-xr-x root/root ./usr/lib/
drwxr-xr-x root/root ./usr/lib/systemd/
drwxr-xr-x root/root ./usr/lib/systemd/system/
-rw-r--r-- root/root ./usr/lib/systemd/system/ff802-cc-auto-enable@.service
-rw-r--r-- root/root ./usr/lib/systemd/system/ff802-cc-auto@.service
drwxr-xr-x root/root ./usr/lib/systemd/user/
-rw-r--r-- root/root ./usr/lib/systemd/user/ff802-cc-notify.path
-rw-r--r-- root/root ./usr/lib/systemd/user/ff802-cc-notify.service
drwxr-xr-x root/root ./usr/lib/systemd/user/graphical-session.target.wants/
lrwxrwxrwx root/root ./usr/lib/systemd/user/graphical-session.target.wants/ff802-cc-notify.path -> ../ff802-cc-notify.path
lrwxrwxrwx root/root ./usr/lib/systemd/user/graphical-session.target.wants/ff802-cc-notify.service -> ../ff802-cc-notify.service
drwxr-xr-x root/root ./usr/lib/udev/
drwxr-xr-x root/root ./usr/lib/udev/rules.d/
-rw-r--r-- root/root ./usr/lib/udev/rules.d/70-ff802-cc-auto.rules
drwxr-xr-x root/root ./usr/libexec/
drwxr-xr-x root/root ./usr/libexec/ff802-cc/
-rwxr-xr-x root/root ./usr/libexec/ff802-cc/ff802-cc-auto-mixer-off
-rwxr-xr-x root/root ./usr/libexec/ff802-cc/ff802-cc-auto-mixer-on
-rwxr-xr-x root/root ./usr/libexec/ff802-cc/ff802-cc-auto-off
-rwxr-xr-x root/root ./usr/libexec/ff802-cc/ff802-cc-auto-on
-rwxr-xr-x root/root ./usr/libexec/ff802-cc/ff802-cc-autoswitch
-rwxr-xr-x root/root ./usr/libexec/ff802-cc/ff802-cc-notify
drwxr-xr-x root/root ./usr/share/
drwxr-xr-x root/root ./usr/share/doc/
drwxr-xr-x root/root ./usr/share/doc/ff802-cc-auto/
-rw-r--r-- root/root ./usr/share/doc/ff802-cc-auto/README.Debian
-rw-r--r-- root/root ./usr/share/doc/ff802-cc-auto/changelog.gz
-rw-r--r-- root/root ./usr/share/doc/ff802-cc-auto/copyright
drwxr-xr-x root/root ./usr/share/polkit-1/
drwxr-xr-x root/root ./usr/share/polkit-1/actions/
-rw-r--r-- root/root ./usr/share/polkit-1/actions/org.ff802cc.auto.policy
EOF
sort "$T/contents.expected.in" > "$T/contents.expected"
ok "package contains exactly the expected files, modes, owners and links" 'cmp -s "$T/contents.txt" "$T/contents.expected"'
cmp -s "$T/contents.txt" "$T/contents.expected" || diff "$T/contents.expected" "$T/contents.txt" | sed 's/^/    /'
ok "no file below /etc, nothing in sudoers, no udev rule granting access" \
    '! grep -q "\./etc/" "$T/contents.txt"'

X=$T/x
dpkg-deb -x "$DEB" "$X"
dpkg-deb -e "$DEB" "$X/DEBIAN"
same=1
for pair in "ff802-cc-auto:usr/bin/ff802-cc-auto" "ff802-cc-auto-on:usr/libexec/ff802-cc/ff802-cc-auto-on" \
            "ff802-cc-auto-off:usr/libexec/ff802-cc/ff802-cc-auto-off" "ff802-cc-notify:usr/libexec/ff802-cc/ff802-cc-notify" \
            "ff802-cc-auto-mixer-on:usr/libexec/ff802-cc/ff802-cc-auto-mixer-on" \
            "ff802-cc-auto-mixer-off:usr/libexec/ff802-cc/ff802-cc-auto-mixer-off" \
            "70-ff802-cc-auto.rules:usr/lib/udev/rules.d/70-ff802-cc-auto.rules" \
            "ff802-cc-auto@.service:usr/lib/systemd/system/ff802-cc-auto@.service" \
            "ff802-cc-auto-enable@.service:usr/lib/systemd/system/ff802-cc-auto-enable@.service" \
            "ff802-cc-notify.path:usr/lib/systemd/user/ff802-cc-notify.path" \
            "ff802-cc-notify.service:usr/lib/systemd/user/ff802-cc-notify.service" \
            "org.ff802cc.auto.policy:usr/share/polkit-1/actions/org.ff802cc.auto.policy" \
            "README.Debian:usr/share/doc/ff802-cc-auto/README.Debian"; do
    cmp -s "$P/${pair%%:*}" "$X/${pair#*:}" || { echo "    differs: ${pair#*:}"; same=0; }
done
ok "packaged files equal the repository" '[ "$same" = 1 ]'

BIN=$X/usr/libexec/ff802-cc/ff802-cc-autoswitch
syms=$(nm -D --undefined-only "$BIN" | awk '{ print $2 }' | sed 's/@.*//')
has() { printf '%s\n' "$syms" | grep -qx "$1"; }
# loops as functions: a "return" inside ok's eval would leave ok itself
none_of() { for s in "$@"; do has "$s" && return 1; done; return 0; }
scripts_parse() { for s in postinst prerm postrm; do sh -n "$X/DEBIAN/$s" || return 1; done; return 0; }
ok "packaged program can write (claim, bulk transfer, release are linked)" \
    'has libusb_claim_interface && has libusb_bulk_transfer && has libusb_release_interface && has libusb_open'
ok "packaged program has no forbidden USB call (reset, detach, config, control transfer, ...)" \
    'none_of libusb_reset_device libusb_detach_kernel_driver libusb_set_auto_detach_kernel_driver \
              libusb_set_configuration libusb_set_interface_alt_setting libusb_control_transfer \
              libusb_interrupt_transfer libusb_submit_transfer libusb_open_device_with_vid_pid \
              libusb_clear_halt'
ok "packaged service program: ALSA sequencer with the mixer write path (FF802_ENABLE_MIXER_WRITE=ON in the package build)" \
    'has snd_seq_open && has snd_seq_query_next_port && has snd_seq_connect_to &&
     has snd_seq_event_output_direct &&
     none_of snd_seq_event_output snd_rawmidi_open snd_rawmidi_write snd_ctl_open snd_ctl_elem_write snd_pcm_open'
MI=$X/usr/bin/ff802-mixer-init
msyms=$(nm -D --undefined-only "$MI" | awk '{ print $2 }' | sed 's/@.*//')
mhas() { printf '%s\n' "$msyms" | grep -qx "$1"; }
mnone_of() { for s in "$@"; do mhas "$s" && return 1; done; return 0; }
ok "packaged ff802-mixer-init: same write path, no USB, no rawmidi/control/PCM, not the simulation" \
    'mhas snd_seq_open && mhas snd_seq_connect_to && mhas snd_seq_event_output_direct &&
     mnone_of libusb_init libusb_open snd_rawmidi_open snd_ctl_open snd_ctl_elem_write snd_pcm_open &&
     ! grep -aq "SIMULATED" "$MI"'
ok "package contains no hardware test program (B.2-B.4, D1, D3, E1)" \
    '[ -z "$(find "$X" -name "ff802-mixer-[bde][0-9]*")" ]'
ok "packaged program is the installed build, not the simulation" \
    '! grep -aq "SIMULATED backend" "$BIN" && ! grep -aq "FF802_AUTO_ROOT must be" "$BIN" &&
     grep -aq "only started by ff802-cc-auto@.service" "$BIN"'

# --- the automatic port reset and the 26 mixer registers in the packaged programs ---
# (the programs are not executed: strings and data of the stripped binaries)
EBIN=$X/usr/libexec/ff802-cc/ff802-cc-autoswitch
PVER=$(sed -n 's/^    VERSION \([0-9.]*\)$/\1/p' "$SRC/CMakeLists.txt" | head -n 1)
ok "packaged ff802-cc-autoswitch is this version ($PVER) with the automatic port reset" \
    '[ -n "$PVER" ] && grep -aq "$PVER" "$EBIN" &&
     grep -aq "Port reset triggered: %s/disable = 1, %u ms, = 0; waiting up to %u ms" "$EBIN" &&
     grep -aq "reset its USB hub port (as ff802-cc enable)" "$EBIN"'
ok "packaged program uses the port reset of usb_port.c (ff802-cc enable): its status texts are linked in" \
    'grep -aq "port reset, device back in the new mode" "$EBIN" &&
     grep -aq "the device came back with another USB ID" "$EBIN" &&
     grep -aq "could not re-enable the USB hub port" "$EBIN"'
ok "sources: one reset implementation, called with the same sequence by ff802-cc and the automation" \
    '[ "$(grep -c "ff802_port_switch(" "$SRC/tools/ff802_cc.c")" = 1 ] &&
     [ "$(grep -c "ff802_port_switch(" "$SRC/src/auto_switch.c")" = 1 ] &&
     grep -q "ff802_port_switch(sysfs, dev, FF802_PORT_OFF_MS, FF802_PORT_WAIT_MS," "$SRC/tools/ff802_cc.c" &&
     [ "$(grep -A1 "ff802_port_switch(p->sysfs_root, d, FF802_PORT_OFF_MS," "$SRC/src/auto_switch.c" | tail -n 1 | tr -s " ")" = " FF802_PORT_WAIT_MS, FF802_MODE_CLASS_COMPLIANT," ] &&
     grep -q "^#define FF802_PORT_OFF_MS   2000u$" "$SRC/include/ff802/usb_port.h" &&
     grep -q "^#define FF802_PORT_WAIT_MS 15000u$" "$SRC/include/ff802/usb_port.h" &&
     ! grep -rqE "\"[^\"]*/disable\"|O_WRONLY" "$SRC/src/auto_switch.c" "$SRC/tools/ff802_cc_autoswitch.c"'
all_strings() { # all_strings <file> <string>...: every string is in the file
    file=$1; shift
    for s in "$@"; do grep -aqF "$s" "$file" || { echo "    missing: $s"; return 1; }; done
    return 0
}
ok "packaged program: the journal distinguishes command, reset, failure, not back, not CC, mixer result" \
    'all_strings "$EBIN" "CC command sent: 01 00 44 BD to %s" "Port reset triggered:" "Port reset failed:" \
         "Port reset done, device not re-enumerated at %s within %u ms." "not class-compliant mode." \
         "Re-enumerated in class-compliant mode:" "No automatic port reset: %s." \
         "mixer: result %s (%u of %u registers): %s" \
         "mixer: configuration %s, %u entries, checksum 0x%08X (verified)." \
         "mixer: pass 1 of 2 complete; pass 2 (the same %u values) in %u ms." "mixer: %sset 0x%04X"'

PU=$X/usr/lib/systemd/system
ok "packaged units: enable unit writes only the port disable attribute, CC unit only ALSA, both without capabilities" \
    '[ "$(grep -E "^(DeviceAllow|ReadWritePaths|CapabilityBoundingSet|ProtectKernelTunables|TimeoutStartSec)=" "$PU/ff802-cc-auto-enable@.service" | tr "\n" ";")" = "TimeoutStartSec=60;CapabilityBoundingSet=;DeviceAllow=char-usb_device rw;ReadWritePaths=-/sys/bus/usb/devices/%i/port/disable;ProtectKernelTunables=yes;" ] &&
     [ "$(grep -E "^(DeviceAllow|ReadWritePaths|CapabilityBoundingSet|ProtectKernelTunables|TimeoutStartSec)=" "$PU/ff802-cc-auto@.service" | tr "\n" ";")" = "TimeoutStartSec=60;CapabilityBoundingSet=;DeviceAllow=char-alsa rw;ProtectKernelTunables=yes;" ] &&
     grep -qx "ACTION==\"add\", ENV{SYSTEMD_WANTS}+=\"ff802-cc-auto-enable@%k.service\"" "$X/usr/lib/udev/rules.d/70-ff802-cc-auto.rules"'

# the 168 entries (ff802-mixer-init-v4), from the register formulas (not from
# the C table): 2 crossed routings off, Playback k -> Output k at 0 dB,
# Output k volume 0 dB (entries 1..26 = v2), Input 9 -> Output k at 0x2666
mix_entries() { # <addr> <value> <channel> <kind> <source>, decimal
    echo "$((0x4220)) 0 9 2 1"; echo "$((0x4261)) 0 10 2 2"
    k=1; while [ $k -le 12 ]; do
        echo "$(( 0x4000 | ((k - 1) << 6) | (0x20 + k - 1) )) $((0x9000)) $k 0 $k"; k=$((k + 1)); done
    k=1; while [ $k -le 12 ]; do
        echo "$(( 0x1E00 + ((k - 1) << 8) )) 0 $k 1 0"; k=$((k + 1)); done
    k=1; while [ $k -le 12 ]; do
        echo "$(( 0x4000 | ((k - 1) << 6) | 8 )) $((0x2666)) $k 3 9"; k=$((k + 1)); done
    for o in 1 2 3 4 5 6 7 8 9 10 11 12; do for p in 1 2 3 4 5 6 7 8 9 10 11 12; do
        [ $p = $o ] && continue; [ "$p-$o" = 1-9 ] && continue; [ "$p-$o" = 2-10 ] && continue
        echo "$(( 0x4000 | ((o - 1) << 6) | (0x20 + p - 1) )) 0 $o 2 $p"; done; done
}
mix_list() { mix_entries | while read -r a v c kd s; do printf '0x%04X=0x%04X ' "$a" "$v"; done; }
le16() { printf '%02x%02x' $(($1 & 255)) $(($1 >> 8)); }
mix_table_hex() { # ff802_mixer_entry: addr, value, channel, 3 pad, kind (int), source, 3 pad
    mix_entries | while read -r a v c kd s; do
        printf '%s%s%02x000000%02x000000%02x000000' "$(le16 "$a")" "$(le16 "$v")" "$c" "$kd" "$s"; done
}
mix_fnv() { # mix_fnv <n>: FNV-1a 32 over address and value of the first n entries
    h=2166136261
    for b in $(mix_entries | head -n "$1" | while read -r a v c kd s; do
                   echo $((a & 255)) $((a >> 8)) $((v & 255)) $((v >> 8)); done); do
        h=$(( ((h ^ b) * 16777619) & 0xFFFFFFFF ))
    done
    printf '0x%08X\n' "$h"
}
hexof() { od -An -v -tx1 "$1" | tr -d " \n"; }
TABLE_HEX=$(mix_table_hex)
V2_LIST="0x4220=0x0000 0x4261=0x0000 0x4020=0x9000 0x4061=0x9000 0x40A2=0x9000 0x40E3=0x9000 0x4124=0x9000 0x4165=0x9000 0x41A6=0x9000 0x41E7=0x9000 0x4228=0x9000 0x4269=0x9000 0x42AA=0x9000 0x42EB=0x9000 0x1E00=0x0000 0x1F00=0x0000 0x2000=0x0000 0x2100=0x0000 0x2200=0x0000 0x2300=0x0000 0x2400=0x0000 0x2500=0x0000 0x2600=0x0000 0x2700=0x0000 0x2800=0x0000 0x2900=0x0000 "
IN9_LIST="0x4008=0x2666 0x4048=0x2666 0x4088=0x2666 0x40C8=0x2666 0x4108=0x2666 0x4148=0x2666 0x4188=0x2666 0x41C8=0x2666 0x4208=0x2666 0x4248=0x2666 0x4288=0x2666 0x42C8=0x2666 "
X130_LIST="0x4021=0x0000 0x4022=0x0000 0x4023=0x0000 0x4024=0x0000 0x4025=0x0000 0x4026=0x0000 0x4027=0x0000 0x4028=0x0000 0x4029=0x0000 0x402A=0x0000 0x402B=0x0000 0x4060=0x0000 0x4062=0x0000 0x4063=0x0000 0x4064=0x0000 0x4065=0x0000 0x4066=0x0000 0x4067=0x0000 0x4068=0x0000 0x4069=0x0000 0x406A=0x0000 0x406B=0x0000 0x40A0=0x0000 0x40A1=0x0000 0x40A3=0x0000 0x40A4=0x0000 0x40A5=0x0000 0x40A6=0x0000 0x40A7=0x0000 0x40A8=0x0000 0x40A9=0x0000 0x40AA=0x0000 0x40AB=0x0000 0x40E0=0x0000 0x40E1=0x0000 0x40E2=0x0000 0x40E4=0x0000 0x40E5=0x0000 0x40E6=0x0000 0x40E7=0x0000 0x40E8=0x0000 0x40E9=0x0000 0x40EA=0x0000 0x40EB=0x0000 0x4120=0x0000 0x4121=0x0000 0x4122=0x0000 0x4123=0x0000 0x4125=0x0000 0x4126=0x0000 0x4127=0x0000 0x4128=0x0000 0x4129=0x0000 0x412A=0x0000 0x412B=0x0000 0x4160=0x0000 0x4161=0x0000 0x4162=0x0000 0x4163=0x0000 0x4164=0x0000 0x4166=0x0000 0x4167=0x0000 0x4168=0x0000 0x4169=0x0000 0x416A=0x0000 0x416B=0x0000 0x41A0=0x0000 0x41A1=0x0000 0x41A2=0x0000 0x41A3=0x0000 0x41A4=0x0000 0x41A5=0x0000 0x41A7=0x0000 0x41A8=0x0000 0x41A9=0x0000 0x41AA=0x0000 0x41AB=0x0000 0x41E0=0x0000 0x41E1=0x0000 0x41E2=0x0000 0x41E3=0x0000 0x41E4=0x0000 0x41E5=0x0000 0x41E6=0x0000 0x41E8=0x0000 0x41E9=0x0000 0x41EA=0x0000 0x41EB=0x0000 0x4221=0x0000 0x4222=0x0000 0x4223=0x0000 0x4224=0x0000 0x4225=0x0000 0x4226=0x0000 0x4227=0x0000 0x4229=0x0000 0x422A=0x0000 0x422B=0x0000 0x4260=0x0000 0x4262=0x0000 0x4263=0x0000 0x4264=0x0000 0x4265=0x0000 0x4266=0x0000 0x4267=0x0000 0x4268=0x0000 0x426A=0x0000 0x426B=0x0000 0x42A0=0x0000 0x42A1=0x0000 0x42A2=0x0000 0x42A3=0x0000 0x42A4=0x0000 0x42A5=0x0000 0x42A6=0x0000 0x42A7=0x0000 0x42A8=0x0000 0x42A9=0x0000 0x42AB=0x0000 0x42E0=0x0000 0x42E1=0x0000 0x42E2=0x0000 0x42E3=0x0000 0x42E4=0x0000 0x42E5=0x0000 0x42E6=0x0000 0x42E7=0x0000 0x42E8=0x0000 0x42E9=0x0000 0x42EA=0x0000 "
ok "the 168 registers: v2 (26), Input 9 (12) = v3, then the 130 other crossed playback routings off" \
    '[ "$(mix_entries | wc -l)" = 168 ] && [ "$(mix_list)" = "$V2_LIST$IN9_LIST$X130_LIST" ]'
ok "checksums computed here: all 168 = 0xC43D2879 (v4), first 38 = 0xEC858A44 (v3), first 26 = 0xC97A359C (v2)" \
    '[ "$(mix_fnv 168)" = 0xC43D2879 ] && [ "$(mix_fnv 38)" = 0xEC858A44 ] && [ "$(mix_fnv 26)" = 0xC97A359C ] &&
     grep -q "^#define FF802_MIXER_INIT_CHECKSUM    0xC43D2879u$" "$SRC/include/ff802/mixer_init.h" &&
     grep -q "^#define FF802_MIXER_INIT_V3_CHECKSUM 0xEC858A44u$" "$SRC/include/ff802/mixer_init.h" &&
     grep -q "^#define FF802_MIXER_INIT_V2_CHECKSUM 0xC97A359Cu$" "$SRC/include/ff802/mixer_init.h"'
ok "packaged service and ff802-mixer-init contain exactly this table, once each (same list, automatic and manual)" \
    '[ "${#TABLE_HEX}" = 5376 ] &&
     [ "$(hexof "$EBIN" | grep -o "$TABLE_HEX" | wc -l)" = 1 ] &&
     [ "$(hexof "$MI" | grep -o "$TABLE_HEX" | wc -l)" = 1 ] &&
     grep -aq "ff802-mixer-init-v4" "$EBIN" && grep -aq "ff802-mixer-init-v4" "$MI" &&
     grep -aq "ff802-mixer-init-v3" "$EBIN" && grep -aq "ff802-mixer-init-v3" "$MI" &&
     grep -aq "ff802-mixer-init-v2" "$EBIN" && grep -aq "ff802-mixer-init-v2" "$MI"'
ok "packaged notification texts: success without reconnecting, reconnect only as recovery" \
    'N=$X/usr/libexec/ff802-cc/ff802-cc-notify;
     grep -qF "summary=\"\$APP – Class-Compliant-Modus aktiv\"" "$N" &&
     grep -qF "summary=\"\$APP – automatische Umschaltung nicht bestätigt\"" "$N" &&
     [ "$(grep -c "trennen und wieder anschließen" "$N")" = 2 ] &&
     ! grep -q "after the switch you will be asked to reconnect" "$X/usr/libexec/ff802-cc/ff802-cc-auto-on" &&
     grep -q "port is reset automatically; no further reconnecting is needed" "$X/usr/libexec/ff802-cc/ff802-cc-auto-on"'
ok "control: name, version, architecture, dependencies, off after installation" \
    'grep -qx "Package: ff802-cc-auto" "$X/DEBIAN/control" && grep -qx "Version: 0.2.1~dev1" "$X/DEBIAN/control" &&
     grep -qx "Architecture: $ARCH" "$X/DEBIAN/control" &&
     grep -qx "Depends: libc6 (>= 2.34), libusb-1.0-0 (>= 2:1.0.22), libasound2t64 (>= 1.2.10), systemd, udev" "$X/DEBIAN/control" &&
     grep -q "The automation is off after installation" "$X/DEBIAN/control"'
ok "md5sums cover every file" \
    '[ "$(cut -d" " -f3 "$X/DEBIAN/md5sums" | sort)" = "$(cd "$X" && find usr -type f | sort)" ] &&
     (cd "$X" && md5sum -c --quiet DEBIAN/md5sums)'
ok "maintainer scripts pass sh -n" 'scripts_parse'
ok "19: maintainer scripts never trigger udev, start or enable units, or run the program" \
    '! grep -Eq "udevadm +trigger|systemctl +(start|restart|enable|reload-or)|ff802-cc-autoswitch|--service" \
         "$X/DEBIAN/postinst" "$X/DEBIAN/prerm" "$X/DEBIAN/postrm"'

# --- isolated root -----------------------------------------------------------------
R=$T/root
mkdir -p "$R/var/lib/dpkg/info" "$R/var/lib/dpkg/updates" "$R/var/log" "$R/run"
: > "$R/var/lib/dpkg/status"
: > "$R/var/lib/dpkg/available"
dpkg_r() { dpkg --root="$R" --force-not-root --force-script-chrootless --log="$T/dpkg.log" "$@"; }
run() { out=$1; shift; "$@" > "$T/$out" 2>&1; echo $?; }
tree_sum() {
    (cd "$R" && find . -path ./var/lib/dpkg -prune -o -path ./var/log -prune -o -print | sort &&
     find . -path ./var/lib/dpkg -prune -o -path ./var/log -prune -o -type f -print | sort | xargs -r cksum)
}
standin() { # standin <package> <version>
    d=$T/standin-$1
    mkdir -p "$d/DEBIAN"
    printf 'Package: %s\nVersion: %s\nArchitecture: %s\nMaintainer: test <test@invalid>\nDescription: stand-in for the T4 package test\n' \
        "$1" "$2" "$ARCH" > "$d/DEBIAN/control"
    dpkg-deb --root-owner-group --build "$d" "$T/$1.deb" > /dev/null
}
standin libc6 2.42-0test
standin libusb-1.0-0 2:1.0.29-0test
standin libasound2t64 1.2.14-0test
standin systemd 259-0test
standin udev 259-0test
rc=$(run standins.txt dpkg_r -i "$T/libc6.deb" "$T/libusb-1.0-0.deb" "$T/libasound2t64.deb" "$T/systemd.deb" "$T/udev.deb")
ok "stand-in dependency packages installed in the isolated root" '[ "$rc" = 0 ]'
before=$(tree_sum)
: > "$TW/calls.log"

rc=$(run install.txt dpkg_r -i "$DEB")
ok "install succeeds; postinst reports the automation as off" \
    '[ "$rc" = 0 ] && grep -q "ff802-cc-auto: automation is off" "$T/install.txt" &&
     dpkg-query --admindir="$R/var/lib/dpkg" -W -f="\${Status}" ff802-cc-auto | grep -qx "install ok installed"'
ok "installation alone: no state, no run directory, no tripwire called (no USB, no udev trigger)" \
    '[ ! -e "$R/var/lib/ff802-cc-auto" ] && [ ! -e "$R/run/ff802-cc-auto" ] && [ -z "$(calls)" ]'
ok "installed files are in place" \
    '[ -x "$R/usr/libexec/ff802-cc/ff802-cc-autoswitch" ] && [ -f "$R/usr/lib/udev/rules.d/70-ff802-cc-auto.rules" ] &&
     [ -L "$R/usr/lib/systemd/user/graphical-session.target.wants/ff802-cc-notify.path" ]'

# turn it on with the installed helper (test root), then upgrade: state kept
FF802_AUTO_ROOT=$R sh "$R/usr/libexec/ff802-cc/ff802-cc-auto-on" > "$T/on.txt" 2>&1
FF802_AUTO_ROOT=$R sh "$R/usr/libexec/ff802-cc/ff802-cc-auto-mixer-on" > "$T/mon.txt" 2>&1
ok "installed mixer helper writes only the mixer-init state" \
    '[ "$(cat "$R/var/lib/ff802-cc-auto/mixer-init")" = on ] && [ "$(cat "$R/var/lib/ff802-cc-auto/mode")" = on ] &&
     [ "$(ls "$R/var/lib/ff802-cc-auto" | tr "\n" " ")" = "mixer-init mode " ]'
DEBUP=$(sh "$SRC/packaging/build-deb-auto.sh" "$T/deb-up" 0.2.1~dev2 2>> "$T/build.err")
rc=$(run upgrade.txt dpkg_r -i "$DEBUP")
ok "upgrade keeps the automation state (on) and the mixer-init state (on) and reports them" \
    '[ "$rc" = 0 ] && [ "$(cat "$R/var/lib/ff802-cc-auto/mode")" = on ] && [ "$(cat "$R/var/lib/ff802-cc-auto/mixer-init")" = on ] &&
     grep -q "automation is on" "$T/upgrade.txt" && grep -q "mixer initialisation is on" "$T/upgrade.txt"'

# --- 20: full deactivation and rollback -----------------------------------------
FF802_AUTO_ROOT=$R sh "$R/usr/libexec/ff802-cc/ff802-cc-auto-off" > "$T/off.txt" 2>&1
ok "20: off with the installed helper" '[ "$(cat "$R/var/lib/ff802-cc-auto/mode")" = off ]'
mkdir -p "$R/run/ff802-cc-auto/attempts"; echo 1 > "$R/run/ff802-cc-auto/event"
rc=$(run remove.txt dpkg_r -r ff802-cc-auto)
ok "20: remove deletes the program, units, rule, policy and the whole state" \
    '[ "$rc" = 0 ] && [ ! -e "$R/usr/libexec/ff802-cc" ] && [ ! -e "$R/usr/lib/udev/rules.d/70-ff802-cc-auto.rules" ] &&
     [ ! -e "$R/usr/share/polkit-1/actions/org.ff802cc.auto.policy" ] &&
     [ ! -e "$R/var/lib/ff802-cc-auto" ] && [ ! -e "$R/run/ff802-cc-auto" ]'
rc=$(run reinstall.txt dpkg_r -i "$DEB")
ok "20: installing again after removal starts with the automation off" \
    '[ "$rc" = 0 ] && [ ! -e "$R/var/lib/ff802-cc-auto/mode" ] && grep -q "automation is off" "$T/reinstall.txt"'
rc=$(run purge.txt dpkg_r -P ff802-cc-auto)
ok "20: purge: the root is exactly as before the installation" \
    '[ "$rc" = 0 ] && [ "$(tree_sum)" = "$before" ] &&
     ! dpkg-query --admindir="$R/var/lib/dpkg" -W ff802-cc-auto > /dev/null 2>&1'
ok "19/20: no tripwire was called by install, upgrade, remove or purge" '[ -z "$(calls)" ]'
calls | sed 's/^/    /'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
