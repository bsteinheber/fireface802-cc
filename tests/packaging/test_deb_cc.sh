#!/bin/sh
# Package test of ff802-cc (ff802-cc, ff802-detect) in an isolated root.
#
# The current source tree (tracked and new files) is copied into a Git
# clone below the scratch directory and committed THERE only, so the
# package is built from a clean checkout as the release process requires;
# the repository itself is not touched.
#
# - two builds of the same commit from two checkouts: byte-identical
# - refusal on uncommitted changes, on uncommitted packaging files, and
#   outside a Git checkout
# - contents, modes, control, md5sums, no maintainer scripts, BUILDINFO
# - the programs: write-capable, only the allowed detach call, not the
#   simulation, version string with the commit's revision
# - dpkg --root: coexistence with ff802-cc-auto, removal of either leaves
#   the other untouched, upgrade, purge; tripwires for systemctl, udevadm,
#   the programs: nothing may call them
#
# Usage: test_deb_cc.sh <source dir> <scratch dir>

set -u
LC_ALL=C
export LC_ALL

SRC=$1
T=$2
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
    */deb-cc-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"
ARCH=$(dpkg --print-architecture)
G="git -c user.name=test -c user.email=test@invalid -c commit.gpgsign=false"

# --- checkouts -------------------------------------------------------------
A=$T/src-a
git clone -q --no-hardlinks "$SRC" "$A"
(cd "$SRC" && git ls-files -co --exclude-standard -z | xargs -0 tar -cf - --no-recursion) |
    (cd "$A" && tar -xf -)
(cd "$A" && $G add -A && { git diff --cached --quiet || $G commit -q -m "test: current tree"; })
REV=$(git -C "$A" rev-parse --short=12 HEAD)
B=$T/src-b
git clone -q --no-hardlinks "$A" "$B"
echo "info test commit $REV (copy of the current tree)"

# --- tripwires ------------------------------------------------------------------
TW=$T/tripwire
mkdir -p "$TW"
for c in systemctl udevadm ff802-cc ff802-detect ff802-cc-autoswitch sudo; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/calls.log"\nexit 0\n' "$c" "$TW" > "$TW/$c"
    chmod +x "$TW/$c"
done
PATH=$TW:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
calls() { cat "$TW/calls.log" 2>/dev/null; }

# --- build -----------------------------------------------------------------------
DEB=$(sh "$A/packaging/build-deb-cc.sh" "$T/deb-a" 2> "$T/build-a.err")
DEB_B=$(sh "$B/packaging/build-deb-cc.sh" "$T/deb-b" 2> "$T/build-b.err")
ok "package builds from a clean checkout" '[ -f "$DEB" ] && [ "${DEB##*/}" = "ff802-cc_0.4.0_${ARCH}.deb" ]'
[ -f "$DEB" ] || { cat "$T/build-a.err"; exit 1; }
ok "same commit from another checkout path: byte-identical package" 'cmp -s "$DEB" "$DEB_B"'
DEB_R=$(sh "$A/packaging/build-deb-cc.sh" "$T/deb-a" 2>> "$T/build-a.err")
ok "rebuild of the same commit later (fresh build directory): byte-identical" \
    '[ "$DEB_R" = "$DEB" ] && cmp -s "$DEB_R" "$DEB_B"'
ok "building called none of the tripwires (programs never executed)" '[ -z "$(calls)" ]'

# --- refusals ---------------------------------------------------------------------
echo "# local change" >> "$B/README.md"
sh "$B/packaging/build-deb-cc.sh" "$T/deb-dirty" > "$T/dirty.txt" 2>&1; rc=$?
ok "uncommitted change in a tracked file: refused (exit 3), no package" \
    '[ "$rc" = 3 ] && grep -q "uncommitted changes in tracked files" "$T/dirty.txt" &&
     grep -q "README.md" "$T/dirty.txt" && ! ls "$T"/deb-dirty/*.deb > /dev/null 2>&1'
(cd "$B" && git add README.md)
sh "$B/packaging/build-deb-cc.sh" "$T/deb-dirty" > "$T/dirty2.txt" 2>&1; rc=$?
ok "staged but uncommitted change: refused (exit 3)" '[ "$rc" = 3 ]'
(cd "$B" && git reset -q --hard)
(cd "$B" && git rm -q --cached packaging/ff802-cc/README.Debian && $G commit -q -m "test: untrack")
sh "$B/packaging/build-deb-cc.sh" "$T/deb-dirty" > "$T/untracked.txt" 2>&1; rc=$?
ok "a packaged file that is not committed: refused (exit 3)" \
    '[ "$rc" = 3 ] && grep -q "packaging/ff802-cc/README.Debian is not committed" "$T/untracked.txt"'
echo "notes" > "$A/my-notes.txt"
DEB_U=$(sh "$A/packaging/build-deb-cc.sh" "$T/deb-u" 2> "$T/untracked2.txt")
ok "other untracked files: listed, not packaged, same bytes" \
    'cmp -s "$DEB_U" "$DEB" && grep -q "untracked (not part of the build): my-notes.txt" "$T/untracked2.txt"'
rm -f "$A/my-notes.txt"
mkdir -p "$T/nogit"
(cd "$A" && git archive HEAD) | (cd "$T/nogit" && tar -xf -)
sh "$T/nogit/packaging/build-deb-cc.sh" "$T/deb-nogit" > "$T/nogit.txt" 2>&1; rc=$?
ok "outside a Git checkout: refused" '[ "$rc" = 2 ] && grep -q "is not a Git checkout" "$T/nogit.txt"'

# --- contents -------------------------------------------------------------------------
dpkg-deb -c "$DEB" | awk '{ print $1, $2, $6 }' | sort > "$T/contents.txt"
cat > "$T/contents.expected.in" <<'EOF'
drwxr-xr-x root/root ./
drwxr-xr-x root/root ./usr/
drwxr-xr-x root/root ./usr/bin/
-rwxr-xr-x root/root ./usr/bin/ff802-cc
-rwxr-xr-x root/root ./usr/bin/ff802-detect
drwxr-xr-x root/root ./usr/share/
drwxr-xr-x root/root ./usr/share/doc/
drwxr-xr-x root/root ./usr/share/doc/ff802-cc/
-rw-r--r-- root/root ./usr/share/doc/ff802-cc/BUILDINFO
-rw-r--r-- root/root ./usr/share/doc/ff802-cc/README.Debian
-rw-r--r-- root/root ./usr/share/doc/ff802-cc/changelog.gz
-rw-r--r-- root/root ./usr/share/doc/ff802-cc/copyright
EOF
sort "$T/contents.expected.in" > "$T/contents.expected"
ok "package contains exactly the expected files, modes and owners" 'cmp -s "$T/contents.txt" "$T/contents.expected"'
cmp -s "$T/contents.txt" "$T/contents.expected" || diff "$T/contents.expected" "$T/contents.txt" | sed 's/^/    /'

X=$T/x
dpkg-deb -x "$DEB" "$X"
dpkg-deb -e "$DEB" "$X/DEBIAN"
ok "control: name, package version, architecture, dependencies, no conflicts" \
    'grep -qx "Package: ff802-cc" "$X/DEBIAN/control" && grep -qx "Version: 0.4.0" "$X/DEBIAN/control" &&
     grep -qx "Architecture: $ARCH" "$X/DEBIAN/control" &&
     grep -qx "Depends: libc6 (>= 2.34), libusb-1.0-0 (>= 2:1.0.22)" "$X/DEBIAN/control" &&
     grep -qx "Suggests: ff802-cc-auto, ff802-ucm-profile" "$X/DEBIAN/control" &&
     ! grep -qE "^(Conflicts|Breaks|Replaces|Pre-Depends|Recommends):" "$X/DEBIAN/control"'
ok "no maintainer scripts (only control and md5sums)" \
    '[ "$(ls "$X/DEBIAN" | tr "\n" " ")" = "control md5sums " ]'
ok "md5sums cover every file" \
    '[ "$(cut -d" " -f3 "$X/DEBIAN/md5sums" | sort)" = "$(cd "$X" && find usr -type f | sort)" ] &&
     (cd "$X" && md5sum -c --quiet DEBIAN/md5sums)'
ok "packaged documentation equals the repository" \
    'cmp -s "$X/usr/share/doc/ff802-cc/README.Debian" "$A/packaging/ff802-cc/README.Debian" &&
     cmp -s "$X/usr/share/doc/ff802-cc/copyright" "$A/packaging/ff802-cc/copyright"'
ok "BUILDINFO names the commit, the revision, the release build and the tool versions" \
    'grep -qx "git commit: $(git -C "$A" rev-parse HEAD)" "$X/usr/share/doc/ff802-cc/BUILDINFO" &&
     grep -qx "git revision: $REV" "$X/usr/share/doc/ff802-cc/BUILDINFO" &&
     grep -qx "project version: 0.4.0" "$X/usr/share/doc/ff802-cc/BUILDINFO" &&
     grep -q "^build type: Release, FF802_ENABLE_USB_WRITE=ON" "$X/usr/share/doc/ff802-cc/BUILDINFO" &&
     grep -q "^compiler: " "$X/usr/share/doc/ff802-cc/BUILDINFO" &&
     grep -q "^libusb-1.0-0-dev: " "$X/usr/share/doc/ff802-cc/BUILDINFO"'

CC_BIN=$X/usr/bin/ff802-cc
DET_BIN=$X/usr/bin/ff802-detect
syms() { nm -D --undefined-only "$1" | awk '{ print $2 }' | sed 's/@.*//'; }
has() { syms "$1" | grep -qx "$2"; }
# all / none of the symbols (functions: a "return" inside ok's eval would
# leave ok itself)
all_syms() { b=$1; shift; for s in "$@"; do has "$b" "$s" || return 1; done; return 0; }
no_syms() { b=$1; shift; for s in "$@"; do has "$b" "$s" && return 1; done; return 0; }
ok "ff802-cc can write: open, claim, bulk transfer, release, libusb auto-detach" \
    'all_syms "$CC_BIN" libusb_open libusb_claim_interface libusb_bulk_transfer \
              libusb_release_interface libusb_set_auto_detach_kernel_driver'
ok "ff802-cc: no other detach, no reset, no configuration change, no control transfer" \
    'no_syms "$CC_BIN" libusb_detach_kernel_driver libusb_reset_device libusb_set_configuration \
              libusb_set_interface_alt_setting libusb_clear_halt libusb_control_transfer \
              libusb_interrupt_transfer libusb_submit_transfer libusb_open_device_with_vid_pid'
ok "ff802-detect cannot open a device or write" \
    'no_syms "$DET_BIN" libusb_open libusb_claim_interface libusb_bulk_transfer \
              libusb_set_auto_detach_kernel_driver && has "$DET_BIN" libusb_get_device_list'
ok "the programs are the release build of this commit, not a simulation" \
    'strings "$CC_BIN" | grep -qx "$REV" && strings "$DET_BIN" | grep -qx "$REV" &&
     strings "$CC_BIN" | grep -qx "Release" &&
     ! grep -aq "SIMULATED" "$CC_BIN" && ! grep -aq "+dirty" "$CC_BIN"'
# --version returns before any USB initialisation (only the build-time
# constants are printed); run with the tripwires in PATH
ver=$("$CC_BIN" --version 2>&1); dver=$("$DET_BIN" --version 2>&1)
ok "ff802-cc --version: project version, revision, build type, write gate" \
    '[ "$ver" = "ff802-cc 0.4.0 (git $REV, Release, USB writes enabled)" ] &&
     [ "$dver" = "ff802-detect 0.4.0 (git $REV, Release)" ]'
echo "info $ver"

# --- isolated root: coexistence with ff802-cc-auto ---------------------------------
AUTO=$(sh "$A/packaging/build-deb-auto.sh" "$T/deb-auto" 2> "$T/build-auto.err")
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
standin() {
    d=$T/standin-$1
    mkdir -p "$d/DEBIAN"
    printf 'Package: %s\nVersion: %s\nArchitecture: %s\nMaintainer: test <test@invalid>\nDescription: stand-in\n' \
        "$1" "$2" "$ARCH" > "$d/DEBIAN/control"
    dpkg-deb --root-owner-group --build "$d" "$T/$1.deb" > /dev/null
}
standin libc6 2.42-0test
standin libusb-1.0-0 2:1.0.29-0test
standin systemd 259-0test
standin udev 259-0test
dpkg_r -i "$T/libc6.deb" "$T/libusb-1.0-0.deb" "$T/systemd.deb" "$T/udev.deb" > "$T/standins.txt" 2>&1
before=$(tree_sum)
: > "$TW/calls.log"

rc=$(run inst-cc.txt dpkg_r -i "$DEB")
ok "ff802-cc installs alone (no dependency on ff802-cc-auto or ff802-ucm-profile)" \
    '[ "$rc" = 0 ] && [ -x "$R/usr/bin/ff802-cc" ] && [ -x "$R/usr/bin/ff802-detect" ]'
rc=$(run inst-auto.txt dpkg_r -i "$AUTO")
ok "ff802-cc-auto installs next to it: no file conflict" \
    '[ "$rc" = 0 ] && ! grep -qi "trying to overwrite" "$T/inst-auto.txt" &&
     [ -x "$R/usr/libexec/ff802-cc/ff802-cc-autoswitch" ] && [ -x "$R/usr/bin/ff802-cc-auto" ]'
ok "file ownership is separate (dpkg -S)" \
    'dpkg-query --admindir="$R/var/lib/dpkg" -S /usr/bin/ff802-cc | grep -qx "ff802-cc: /usr/bin/ff802-cc" &&
     dpkg-query --admindir="$R/var/lib/dpkg" -S /usr/bin/ff802-cc-auto | grep -qx "ff802-cc-auto: /usr/bin/ff802-cc-auto"'
FF802_AUTO_ROOT=$R sh "$R/usr/libexec/ff802-cc/ff802-cc-auto-on" > /dev/null 2>&1
auto_files=$(cd "$R" && find usr/libexec usr/lib/udev usr/lib/systemd usr/share/polkit-1 usr/bin/ff802-cc-auto \
             var/lib/ff802-cc-auto -print 2>/dev/null | sort | xargs -r cksum 2>/dev/null)
rc=$(run rm-cc.txt dpkg_r -r ff802-cc)
ok "removing ff802-cc leaves ff802-cc-auto and its state (on) untouched" \
    '[ "$rc" = 0 ] && [ ! -e "$R/usr/bin/ff802-cc" ] && [ ! -e "$R/usr/bin/ff802-detect" ] &&
     [ "$(cd "$R" && find usr/libexec usr/lib/udev usr/lib/systemd usr/share/polkit-1 usr/bin/ff802-cc-auto \
          var/lib/ff802-cc-auto -print 2>/dev/null | sort | xargs -r cksum 2>/dev/null)" = "$auto_files" ] &&
     [ "$(cat "$R/var/lib/ff802-cc-auto/mode")" = on ]'
rc=$(run inst-cc2.txt dpkg_r -i "$DEB")
DEB41=$(sh "$A/packaging/build-deb-cc.sh" "$T/deb-41" 0.4.1 2>> "$T/build-a.err")
rc2=$(run up-cc.txt dpkg_r -i "$DEB41")
ok "upgrade 0.4.0 -> 0.4.1 (package version only; program still reports project 0.4.0)" \
    '[ "$rc" = 0 ] && [ "$rc2" = 0 ] &&
     dpkg-query --admindir="$R/var/lib/dpkg" -W -f="\${Version}" ff802-cc | grep -qx 0.4.1 &&
     "$R/usr/bin/ff802-cc" --version | grep -q "^ff802-cc 0.4.0 (git $REV, "'
rc=$(run rm-auto.txt dpkg_r -P ff802-cc-auto)
ok "purging ff802-cc-auto leaves ff802-cc untouched" \
    '[ "$rc" = 0 ] && [ -x "$R/usr/bin/ff802-cc" ] && [ -x "$R/usr/bin/ff802-detect" ] &&
     [ ! -e "$R/usr/bin/ff802-cc-auto" ] && [ ! -e "$R/var/lib/ff802-cc-auto" ]'
rc=$(run purge-cc.txt dpkg_r -P ff802-cc)
ok "purge: the root is exactly as before" \
    '[ "$rc" = 0 ] && [ "$(tree_sum)" = "$before" ]'
ok "no tripwire was called by install, upgrade, remove or purge" '[ -z "$(calls)" ]'
calls | sed 's/^/    /'

echo "$checks checks, $fails failures"
[ "$fails" -eq 0 ]
