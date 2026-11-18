#!/bin/sh
# Package test of ff802-ucm-profile in an isolated root (no root rights,
# nothing outside the scratch directory is touched).
#
# dpkg runs with --root=<scratch root> --force-not-root
# --force-script-chrootless, so the maintainer scripts work on DPKG_ROOT.
# Stand-in packages for alsa-ucm-conf (with the complete real UCM tree) and
# libasound2t64 satisfy the dependencies; a file conflict with
# alsa-ucm-conf would therefore be detected by dpkg itself.
#
# Usage: test_deb.sh <source dir> <system ucm2> <ucm_query dir or -> <scratch dir>

set -u

SRC=$1
SYS=$2
BIN=$3
T=$4
fails=0
checks=0
LONGNAME="RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed"
TARGET="../../../../ff802-ucm/longname-master.conf"

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
    */deb-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
[ -f "$SYS/USB-Audio/USB-Audio.conf" ] || { echo "system UCM tree not found: $SYS" >&2; exit 2; }

R=$T/root
CONFD=$R/usr/share/alsa/ucm2/conf.d/USB-Audio
BIND=$R/usr/sbin/ff802-ucm-bind
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
unset FF802_UCM_ALSA_LIB_VERSION

dpkg_r() { dpkg --root="$R" --force-not-root --force-script-chrootless --log="$T/dpkg.log" "$@"; }
run() { # run <output file> <command...>: output and exit status
    out=$1; shift
    "$@" > "$T/$out" 2>&1
    echo $?
}
tree_sum() { # contents of the root without the dpkg database
    (cd "$R" && find . -path ./var/lib/dpkg -prune -o -print | LC_ALL=C sort &&
     find . -path ./var/lib/dpkg -prune -o \( -type f -o -type l \) -print | LC_ALL=C sort |
        while IFS= read -r f; do
            if [ -L "$f" ]; then echo "L $f -> $(readlink "$f")"; else cksum "$f"; fi
        done)
}

# --- fake /proc/asound ----------------------------------------------------------
mkproc() { # mkproc <dir> <long name> [<usbid>]
    mkdir -p "$1/card0" "$1/card1"
    echo "${3:-0424:3fdd}" > "$1/card1/usbid"
    echo F80224047128 > "$1/card1/id"
    echo sofsoundwire > "$1/card0/id"
    printf ' 0 [sofsoundwire   ]: sof-soundwire - sof-soundwire\n                      DellInc.-XPS179730-0C4XC6\n 1 [F80224047128   ]: USB-Audio - Fireface 802 (24047128)\n                      %s\n' "$2" > "$1/cards"
}
mkdir -p "$T/proc-none"
printf ' 0 [sofsoundwire   ]: sof-soundwire - sof-soundwire\n                      DellInc.-XPS179730-0C4XC6\n' > "$T/proc-none/cards"
mkproc "$T/proc-ff802" "$LONGNAME"
mkproc "$T/proc-otherport" "RME Fireface 802 (24047128) at usb-0000:00:14.0-1, high speed"
mkproc "$T/proc-port3" "RME Fireface 802 (24047128) at usb-0000:00:14.0-3, high speed"
mkproc "$T/proc-fs" "RME Fireface 802 FS (12345678) at usb-0000:00:14.0-1, high speed"
mkproc "$T/proc-slash" "RME Fireface 802 (1) at usb-a/../b, high speed"
mkproc "$T/proc-two" "$LONGNAME"
mkdir -p "$T/proc-two/card2"
echo 0424:3fdd > "$T/proc-two/card2/usbid"
echo F802B > "$T/proc-two/card2/id"
printf ' 2 [F802B          ]: USB-Audio - Fireface 802 (2)\n                      RME Fireface 802 (2) at usb-0000:00:14.0-3, high speed\n' >> "$T/proc-two/cards"

# --- 1. build, reproducibility, contents ---------------------------------------
DEB=$(sh "$SRC/packaging/build-deb.sh" "$T/out1")
DEB2=$(sh "$SRC/packaging/build-deb.sh" "$T/out2")
ok "package builds" '[ -f "$DEB" ] && [ "${DEB##*/}" = "ff802-ucm-profile_0.3.0_all.deb" ]'
ok "build is reproducible (identical bytes)" '[ -f "$DEB2" ] && cmp -s "$DEB" "$DEB2"'
SOURCE_DATE_EPOCH=1700000000 sh "$SRC/packaging/build-deb.sh" "$T/out3" > /dev/null
ok "SOURCE_DATE_EPOCH changes only the timestamps" \
    '! cmp -s "$DEB" "$T/out3/${DEB##*/}" && [ "$(dpkg-deb -c "$DEB" | awk "{print \$1, \$2, \$3, \$6}")" = "$(dpkg-deb -c "$T/out3/${DEB##*/}" | awk "{print \$1, \$2, \$3, \$6}")" ]'
dpkg-deb -c "$DEB" | awk '$1 !~ /^d/ {print $1, $2, $6}' | sort > "$T/contents.txt"
cat > "$T/contents.expected" <<'EOF'
-rw-r--r-- root/root ./usr/share/alsa/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf
-rw-r--r-- root/root ./usr/share/alsa/ucm2/USB-Audio/RME/Fireface-802.conf
-rw-r--r-- root/root ./usr/share/alsa/ucm2/USB-Audio/conf.d/0424-3fdd.conf
-rw-r--r-- root/root ./usr/share/doc/ff802-ucm-profile/README.Debian
-rw-r--r-- root/root ./usr/share/doc/ff802-ucm-profile/changelog.gz
-rw-r--r-- root/root ./usr/share/doc/ff802-ucm-profile/copyright
-rw-r--r-- root/root ./usr/share/ff802-ucm/longname-master.conf
-rw-r--r-- root/root ./usr/lib/udev/rules.d/77-ff802-ucm-bind.rules
-rwxr-xr-x root/root ./usr/sbin/ff802-ucm-bind
EOF
sort -o "$T/contents.expected" "$T/contents.expected"
ok "package contains exactly the expected files, modes and owners" 'cmp -s "$T/contents.txt" "$T/contents.expected"'
ok "profile files in the package equal the repository" \
    'mkdir -p "$T/x" && dpkg-deb -x "$DEB" "$T/x" &&
     cmp -s "$T/x/usr/share/alsa/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf" "$SRC/audio/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf" &&
     cmp -s "$T/x/usr/share/alsa/ucm2/USB-Audio/RME/Fireface-802.conf" "$SRC/audio/ucm2/USB-Audio/RME/Fireface-802.conf" &&
     cmp -s "$T/x/usr/share/alsa/ucm2/USB-Audio/conf.d/0424-3fdd.conf" "$SRC/audio/ucm2/USB-Audio/conf.d/0424-3fdd.conf"'
ok "no package file exists in the Ubuntu UCM tree (nothing is overwritten)" \
    '! (cd "$T/x/usr/share/alsa/ucm2" && find . -type f) | while IFS= read -r f; do [ -e "$SYS/$f" ] && echo "$f"; done | grep -q .'
ok "control: name, version, architecture, dependencies" \
    'dpkg-deb -f "$DEB" Package Version Architecture Depends | tr "\n" "|" | grep -qx "Package: ff802-ucm-profile|Version: 0.3.0|Architecture: all|Depends: alsa-ucm-conf (>= 1.2.15.3), libasound2t64 (>= 1.2.15.3)|"'
ok "md5sums cover every file" \
    '[ "$(dpkg-deb -I "$DEB" md5sums | wc -l)" = 9 ] && (cd "$T/x" && dpkg-deb -I "$DEB" md5sums | md5sum -c --quiet)'
ok "bind tool knows the package version" '"$T/x/usr/sbin/ff802-ucm-bind" --version | grep -qx "ff802-ucm-bind 0.3.0"'
ok "maintainer scripts and bind tool pass sh -n" \
    'for s in postinst prerm postrm ff802-ucm-bind; do sh -n "$SRC/packaging/ff802-ucm/$s" || exit 1; done'
ok "udev rule in the package equals the repository" \
    'cmp -s "$T/x/usr/lib/udev/rules.d/77-ff802-ucm-bind.rules" "$SRC/packaging/ff802-ucm/77-ff802-ucm-bind.rules"'
ok "maintainer scripts only reload udev rules: no trigger, no service start or restart" \
    '! grep -Eq "udevadm +trigger|udevadm +settle|systemctl|wireplumber|pipewire" "$SRC/packaging/ff802-ucm/postinst" "$SRC/packaging/ff802-ucm/prerm" "$SRC/packaging/ff802-ucm/postrm" &&
     [ "$(grep -c "udevadm control --reload-rules" "$SRC/packaging/ff802-ucm/postinst" "$SRC/packaging/ff802-ucm/postrm" | cut -d: -f2 | tr "\n" " ")" = "1 1 " ]'

# --- 2. isolated root with stand-in dependencies ----------------------------------
mkdir -p "$R/var/lib/dpkg/info" "$R/var/lib/dpkg/updates" "$R/var/lib/dpkg/triggers"
: > "$R/var/lib/dpkg/status"
: > "$R/var/lib/dpkg/available"
stub() { # stub <name> <version> [<ucm2 tree>]
    d=$T/stub-$1
    mkdir -p "$d/DEBIAN"
    printf 'Package: %s\nVersion: %s\nArchitecture: all\nMaintainer: test <test@invalid>\nDescription: stand-in for %s\n' \
        "$1" "$2" "$1" > "$d/DEBIAN/control"
    if [ -n "${3:-}" ]; then
        mkdir -p "$d/usr/share/alsa"
        cp -RP "$3" "$d/usr/share/alsa/ucm2"
    fi
    dpkg-deb --root-owner-group -b "$d" "$T/$1.deb" > /dev/null
}
stub alsa-ucm-conf 1.2.15.3-1ubuntu1.7 "$SYS"
stub libasound2t64 1.2.15.3-1ubuntu1.1
rc=$(run stubs.txt dpkg_r -i "$T/alsa-ucm-conf.deb" "$T/libasound2t64.deb")
ok "stand-in alsa-ucm-conf (complete UCM tree) installed" '[ "$rc" = 0 ] && [ -f "$R/usr/share/alsa/ucm2/USB-Audio/USB-Audio.conf" ]'
sys_before=$(cd "$R/usr/share/alsa/ucm2" && find . -type f -exec cksum {} + | sort)
tree_before=$(tree_sum)

# --- 3. installation without a card ------------------------------------------------
FF802_UCM_PROC_ASOUND=$T/proc-none
export FF802_UCM_PROC_ASOUND
rc=$(run install.txt dpkg_r -i "$DEB")
ok "install without a connected card succeeds" '[ "$rc" = 0 ]'
ok "postinst reports the missing card" 'grep -q "no original Fireface 802 in CC mode found" "$T/install.txt"'
ok "package is installed and configured" \
    'dpkg-query --admindir="$R/var/lib/dpkg" -W -f="\${Status} \${Version}" ff802-ucm-profile | grep -qx "install ok installed 0.3.0"'
ok "no binding without a card" '[ -z "$(ls -A "$CONFD" | grep -v "^USB-Audio.conf$")" ] && [ ! -e "$R/var/lib/ff802-ucm/bindings" ]'
ok "files of alsa-ucm-conf unchanged" '[ "$(cd "$R/usr/share/alsa/ucm2" && find . -type f -exec cksum {} + | sort | grep -v "/RME/Fireface-802\|/USB-Audio/conf.d/0424-3fdd")" = "$sys_before" ]'

# --- 4. binding --------------------------------------------------------------------
export DPKG_ROOT="$R"
FF802_UCM_PROC_ASOUND=$T/proc-ff802
rc=$(run bind.txt "$BIND" bind)
ok "bind succeeds for the connected card" '[ "$rc" = 0 ] && grep -q "bound: $LONGNAME" "$T/bind.txt"'
ok "binding is a relative link to the master file" \
    '[ -L "$CONFD/$LONGNAME.conf" ] && [ "$(readlink "$CONFD/$LONGNAME.conf")" = "$TARGET" ] && [ -f "$CONFD/$LONGNAME.conf" ]'
ok "binding recorded" '[ "$(cat "$R/var/lib/ff802-ucm/bindings")" = "$LONGNAME" ]'
rc=$(run bind2.txt "$BIND" bind)
ok "second bind is idempotent" '[ "$rc" = 0 ] && grep -q "already bound" "$T/bind2.txt" && [ "$(wc -l < "$R/var/lib/ff802-ucm/bindings")" = 1 ]'
rc=$(run status.txt "$BIND" status)
ok "status shows the bound card and the needed workaround" \
    '[ "$rc" = 0 ] && grep -q "card 1 (F80224047128): $LONGNAME: bound" "$T/status.txt" && grep -q "workaround: needed" "$T/status.txt" && grep -q "alsa-lib: 1.2.15.3-1ubuntu1.1" "$T/status.txt"'

# --- 5. the installed tree, evaluated by the real alsa-lib ---------------------------
if [ "$BIN" != "-" ] && [ -x "$BIN/ucm_query" ]; then
    cat > "$T/alsa.conf" <<EOF
</usr/share/alsa/alsa.conf>
ctl_type.ff802fake.lib "$BIN/libfake_ctl_ff802.so"
ctl.!hw { @args [ CARD ] @args.CARD { type string } type ff802fake card \$CARD }
EOF
    q() { out=$1; shift
          ALSA_CONFIG_PATH=$T/alsa.conf ALSA_CONFIG_UCM2=$R/usr/share/alsa/ucm2 "$BIN/ucm_query" "$@" 2>&1 |
              sed 's/^ucm_query: /alsaucm: /' > "$T/$out"; }
    q verbs.txt -c hw:1 list _verbs
    q devices.txt -c hw:1 list _devices/HiFi
    q dump.txt -c hw:1 dump text
    q dump-split.txt -c "<<<SplitPCM=1>>>hw:1" dump text
    for f in verbs devices dump dump-split; do
        ok "installed package: $f identical to host T1 run 2" \
            'cmp -s "$T/$f.txt" "$SRC/tests/audio/fixtures/t1/$f.txt"'
    done
    ok "installed package: USB-Audio.conf of alsa-ucm-conf untouched" \
        'cmp -s "$R/usr/share/alsa/ucm2/USB-Audio/USB-Audio.conf" "$SYS/USB-Audio/USB-Audio.conf"'
    FF802_FAKE_LONGNAME="RME Fireface 802 (24047128) at usb-0000:00:14.0-1, high speed" q port.txt -c hw:1 list _verbs
    ok "installed package: card on another USB port is not bound (unchanged behaviour)" \
        'grep -q "UCM is not supported" "$T/port.txt"'
    FF802_FAKE_LONGNAME="RME Fireface 802 FS (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed" q fs.txt -c hw:1 list _verbs
    ok "installed package: 802 FS is not selected" 'grep -q "UCM is not supported" "$T/fs.txt"'
else
    echo "skip alsa-lib evaluation of the installed tree (ucm_query not built)"
fi

# --- 6. refusals ---------------------------------------------------------------------
FF802_UCM_PROC_ASOUND=$T/proc-fs
rc=$(run fs.txt "$BIND" bind)
ok "802 FS is refused (exit 3)" '[ "$rc" = 3 ] && grep -q "no original Fireface 802" "$T/fs.txt"'
FF802_UCM_PROC_ASOUND=$T/proc-two
rc=$(run two.txt "$BIND" bind)
ok "two cards: refused (exit 2)" '[ "$rc" = 2 ] && grep -q "refusing to guess" "$T/two.txt"'
FF802_UCM_PROC_ASOUND=$T/proc-slash
rc=$(run slash.txt "$BIND" bind)
ok "long name with '/' is refused" '[ "$rc" = 2 ] && grep -q "cannot be used as a file name" "$T/slash.txt"'
OTHER="RME Fireface 802 (24047128) at usb-0000:00:14.0-1, high speed"
echo "foreign" > "$CONFD/$OTHER.conf"
FF802_UCM_PROC_ASOUND=$T/proc-otherport
rc=$(run foreign.txt "$BIND" bind)
ok "existing foreign file is never overwritten (exit 4)" \
    '[ "$rc" = 4 ] && [ "$(cat "$CONFD/$OTHER.conf")" = foreign ] && grep -q "not created by" "$T/foreign.txt"'
rm -f "$CONFD/$OTHER.conf"
chmod a-w "$CONFD"
rc=$(run ro.txt "$BIND" bind)
chmod u+w "$CONFD"
ok "unwritable directory: error with hint, nothing created" \
    '[ "$rc" = 2 ] && grep -q "run with sudo" "$T/ro.txt" && [ ! -e "$CONFD/$OTHER.conf" ]'
FF802_UCM_ALSA_LIB_VERSION=1.2.16-1 rc=$(run new.txt env FF802_UCM_ALSA_LIB_VERSION=1.2.16-1 "$BIND" bind)
ok "alsa-lib 1.2.16: no binding needed, nothing created" \
    '[ "$rc" = 0 ] && grep -q "no binding needed" "$T/new.txt" && [ ! -e "$CONFD/$OTHER.conf" ]'
rc=$(run newst.txt env FF802_UCM_ALSA_LIB_VERSION=1.2.16-1 "$BIND" status)
ok "alsa-lib 1.2.16: status says the workaround is not needed" 'grep -q "workaround: not needed" "$T/newst.txt"'
rc=$(run force.txt env FF802_UCM_ALSA_LIB_VERSION=1.2.16-1 "$BIND" bind --force)
ok "bind --force binds anyway (second port)" '[ "$rc" = 0 ] && [ -L "$CONFD/$OTHER.conf" ] && [ "$(wc -l < "$R/var/lib/ff802-ucm/bindings")" = 2 ]'
rc=$(run bad.txt "$BIND" frobnicate)
ok "unknown command: usage, exit 2" '[ "$rc" = 2 ] && grep -q "Usage:" "$T/bad.txt"'

# --- 7. upgrade keeps the bindings ------------------------------------------------------
sh "$SRC/packaging/build-deb.sh" "$T/out-new" 0.3.1 > "$T/build-new.txt"
FF802_UCM_PROC_ASOUND=$T/proc-ff802
rc=$(run upgrade.txt dpkg_r -i "$T/out-new/ff802-ucm-profile_0.3.1_all.deb")
ok "upgrade to 0.3.1 succeeds" \
    '[ "$rc" = 0 ] && dpkg-query --admindir="$R/var/lib/dpkg" -W -f="\${Version}" ff802-ucm-profile | grep -qx 0.3.1'
ok "bindings survive the upgrade and point to the new master file" \
    '[ -f "$CONFD/$LONGNAME.conf" ] && [ -f "$CONFD/$OTHER.conf" ] && [ "$(wc -l < "$R/var/lib/ff802-ucm/bindings")" = 2 ] && grep -q "already bound" "$T/upgrade.txt"'
ok "bind tool reports the new version" '"$BIND" --version | grep -qx "ff802-ucm-bind 0.3.1"'

# --- 8. removal -------------------------------------------------------------------------
PORT3="RME Fireface 802 (24047128) at usb-0000:00:14.0-3, high speed"
rc=$(run udev.txt env FF802_UCM_PROC_ASOUND="$T/proc-port3" FF802_UCM_ALSA_LIB_VERSION=1.2.15.3-1ubuntu1.1 "$BIND" udev 1)
ok "installed udev hook binds the card on a third USB port; old bindings stay" \
    '[ "$rc" = 0 ] && [ -L "$CONFD/$PORT3.conf" ] && [ -f "$CONFD/$LONGNAME.conf" ] && [ -f "$CONFD/$OTHER.conf" ] &&
     [ "$(wc -l < "$R/var/lib/ff802-ucm/bindings")" = 3 ]'
echo "foreign" > "$CONFD/foreign.conf"
rc=$(run remove.txt dpkg_r -r ff802-ucm-profile)
ok "remove succeeds" '[ "$rc" = 0 ]'
ok "remove deletes all bindings" '[ ! -e "$CONFD/$LONGNAME.conf" ] && [ ! -L "$CONFD/$LONGNAME.conf" ] && [ ! -L "$CONFD/$OTHER.conf" ] && [ ! -L "$CONFD/$PORT3.conf" ]'
ok "remove leaves foreign files alone" '[ "$(cat "$CONFD/foreign.conf")" = foreign ]'
ok "remove deletes the package files and the state" \
    '[ ! -e "$R/usr/share/ff802-ucm" ] && [ ! -e "$BIND" ] && [ ! -e "$R/usr/lib/udev/rules.d/77-ff802-ucm-bind.rules" ] && [ ! -e "$R/usr/share/alsa/ucm2/USB-Audio/conf.d" ] && [ ! -e "$R/var/lib/ff802-ucm" ]'
rm -f "$CONFD/foreign.conf"
ok "root equals the state before the installation" '[ "$(tree_sum)" = "$tree_before" ]'

# --- 9. purge, and cleanup when the bind tool fails in prerm ------------------------------
rc=$(run inst2.txt dpkg_r -i "$DEB")
"$BIND" bind > /dev/null 2>&1
ok "reinstall and bind" '[ "$rc" = 0 ] && [ -L "$CONFD/$LONGNAME.conf" ]'
rc=$(run purge.txt dpkg_r -P ff802-ucm-profile)
ok "purge removes package, bindings and /var/lib/ff802-ucm" \
    '[ "$rc" = 0 ] && [ ! -L "$CONFD/$LONGNAME.conf" ] && [ ! -e "$R/var/lib/ff802-ucm" ] && [ "$(tree_sum)" = "$tree_before" ]'
rc=$(run inst3.txt dpkg_r -i "$DEB")
"$BIND" bind > /dev/null 2>&1
rm -f "$BIND"   # prerm cannot run the tool: postrm must still clean up
rc=$(run remove3.txt dpkg_r -r ff802-ucm-profile)
ok "without the bind tool, postrm still removes the bindings" \
    '[ "$rc" = 0 ] && grep -q "unbind failed" "$T/remove3.txt" && [ ! -L "$CONFD/$LONGNAME.conf" ] && [ "$(tree_sum)" = "$tree_before" ]'
ok "files of alsa-ucm-conf unchanged at the end" \
    '[ "$(cd "$R/usr/share/alsa/ucm2" && find . -type f -exec cksum {} + | sort)" = "$sys_before" ]'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
