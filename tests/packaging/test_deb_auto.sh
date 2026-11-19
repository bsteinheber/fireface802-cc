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
ok "package builds" '[ -f "$DEB" ] && [ "${DEB##*/}" = "ff802-cc-auto_0.1.2_${ARCH}.deb" ]'
[ -f "$DEB" ] || { cat "$T/build.err"; exit 1; }
ok "build is reproducible (identical bytes in another directory)" 'cmp -s "$DEB" "$DEB2"'
ok "19: building the package called no tripwire (nothing executed, no system access)" '[ -z "$(calls)" ]'
ok "19: the build script never executes the program (only compiles, strips, copies it)" \
    '[ "$(grep -n "ff802-cc-autoswitch" "$SRC/packaging/build-deb-auto.sh" | grep -v "^[0-9]*:#" |
          grep -vE "^[0-9]+:cmake --build .* --target ff802-cc-autoswitch |^[0-9]+:put 0755 |^[0-9]+:strip --strip-unneeded " | wc -l)" = 0 ]'

dpkg-deb -c "$DEB" | awk '{ print $1, $2, $6, $7, $8 }' | sed 's/ *$//' | sort > "$T/contents.txt"
cat > "$T/contents.expected.in" <<'EOF'
drwxr-xr-x root/root ./
drwxr-xr-x root/root ./usr/
drwxr-xr-x root/root ./usr/bin/
-rwxr-xr-x root/root ./usr/bin/ff802-cc-auto
drwxr-xr-x root/root ./usr/lib/
drwxr-xr-x root/root ./usr/lib/systemd/
drwxr-xr-x root/root ./usr/lib/systemd/system/
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
            "70-ff802-cc-auto.rules:usr/lib/udev/rules.d/70-ff802-cc-auto.rules" \
            "ff802-cc-auto@.service:usr/lib/systemd/system/ff802-cc-auto@.service" \
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
ok "packaged program is the installed build, not the simulation" \
    '! grep -aq "SIMULATED backend" "$BIN" && ! grep -aq "FF802_AUTO_ROOT must be" "$BIN" &&
     grep -aq "only started by ff802-cc-auto@.service" "$BIN"'
ok "control: name, version, architecture, dependencies, off after installation" \
    'grep -qx "Package: ff802-cc-auto" "$X/DEBIAN/control" && grep -qx "Version: 0.1.2" "$X/DEBIAN/control" &&
     grep -qx "Architecture: $ARCH" "$X/DEBIAN/control" &&
     grep -qx "Depends: libc6 (>= 2.34), libusb-1.0-0 (>= 2:1.0.22), systemd, udev" "$X/DEBIAN/control" &&
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
standin systemd 259-0test
standin udev 259-0test
rc=$(run standins.txt dpkg_r -i "$T/libc6.deb" "$T/libusb-1.0-0.deb" "$T/systemd.deb" "$T/udev.deb")
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
DEBUP=$(sh "$SRC/packaging/build-deb-auto.sh" "$T/deb-up" 0.1.3 2>> "$T/build.err")
rc=$(run upgrade.txt dpkg_r -i "$DEBUP")
ok "upgrade keeps the automation state (on) and reports it" \
    '[ "$rc" = 0 ] && [ "$(cat "$R/var/lib/ff802-cc-auto/mode")" = on ] &&
     grep -q "automation is on" "$T/upgrade.txt"'

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
