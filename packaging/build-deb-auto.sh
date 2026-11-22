#!/bin/sh
# Build the ff802-cc-auto Debian package (T4, reproducible).
#
# Usage: packaging/build-deb-auto.sh <output dir> [<version override>]
#
# Builds ff802-cc-autoswitch and ff802-mixer-init in <output dir>/.build-auto
# as a Release build with FF802_ENABLE_USB_WRITE=ON (the installed service
# must be able to write). The programs are compiled, stripped and copied,
# never executed: building or checking the package sends nothing to any device.
# Version and SOURCE_DATE_EPOCH come from packaging/ff802-cc-auto/changelog.
# The mixer initialisation (ff802-cc-autoswitch and ff802-mixer-init) is
# built with the ALSA sequencer backend and FF802_ENABLE_MIXER_WRITE=ON:
# this package build is the one place where the production write gate is
# opened (the CMake default stays OFF; the hardware test programs B.2/B.3
# are not built). At run time the automatic initialisation stays off until
# "ff802-cc-auto mixer-init on".
# Needs: cmake, a C compiler, libusb-1.0-0-dev, libasound2-dev, dpkg-deb,
# gzip, md5sum, nm.

set -eu

SRC=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PKGSRC=$SRC/packaging/ff802-cc-auto
OUT=${1:?usage: build-deb-auto.sh <output dir> [<version>]}
NAME=ff802-cc-auto

head=$(sed -n '1p' "$PKGSRC/changelog")
VERSION=$(printf '%s\n' "$head" | sed -n 's/^ff802-cc-auto (\([^)]*\)).*/\1/p')
[ -n "$VERSION" ] || { echo "build-deb-auto: cannot read the version from changelog" >&2; exit 2; }
VERSION=${2:-$VERSION}
case $VERSION in
    *[!0-9A-Za-z.+~-]*|"") echo "build-deb-auto: invalid version '$VERSION'" >&2; exit 2 ;;
esac
trailer=$(sed -n 's/^ -- .*>  \(.*\)$/\1/p' "$PKGSRC/changelog" | head -n 1)
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(date -d "$trailer" +%s)}
export SOURCE_DATE_EPOCH
ARCH=$(dpkg --print-architecture)

mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)

# --- program (compiled only) ------------------------------------------------------
B=$OUT/.build-auto
cmake -S "$SRC" -B "$B" -DCMAKE_BUILD_TYPE=Release -DFF802_WERROR=ON \
      -DBUILD_TESTING=OFF -DFF802_ENABLE_USB_WRITE=ON -DFF802_ENABLE_MIXER_WRITE=ON \
      -DFF802_BUILD_MIXER_B2=OFF -DFF802_BUILD_MIXER_B3=OFF \
      > "$B.configure.log" 2>&1 ||
    { cat "$B.configure.log" >&2; exit 1; }
cmake --build "$B" --target ff802-cc-autoswitch ff802-mixer-init -j > "$B.build.log" 2>&1 ||
    { cat "$B.build.log" >&2; exit 1; }
# the package needs the mixer backend with its write path (nm, nothing executed)
for p in ff802-cc-autoswitch ff802-mixer-init; do
    nm -D --undefined-only "$B/$p" | grep -q ' snd_seq_connect_to' ||
        { echo "build-deb-auto: $p lacks the ALSA sequencer write path (libasound2-dev missing?)" >&2; exit 1; }
done

# --- stage --------------------------------------------------------------------------
STAGE=$OUT/.stage-$NAME
rm -rf -- "${STAGE:?}"
umask 022
mkdir -p "$STAGE/DEBIAN"

put() { # put <mode> <source> <path in package>
    mkdir -p "$STAGE/$(dirname -- "$3")"
    cp -- "$2" "$STAGE/$3"
    chmod "$1" "$STAGE/$3"
}
LX=usr/libexec/ff802-cc
put 0755 "$B/ff802-cc-autoswitch"           $LX/ff802-cc-autoswitch
strip --strip-unneeded "$STAGE/$LX/ff802-cc-autoswitch"
put 0755 "$B/ff802-mixer-init"              usr/bin/ff802-mixer-init
strip --strip-unneeded "$STAGE/usr/bin/ff802-mixer-init"
put 0755 "$PKGSRC/ff802-cc-auto-on"         $LX/ff802-cc-auto-on
put 0755 "$PKGSRC/ff802-cc-auto-off"        $LX/ff802-cc-auto-off
put 0755 "$PKGSRC/ff802-cc-auto-mixer-on"   $LX/ff802-cc-auto-mixer-on
put 0755 "$PKGSRC/ff802-cc-auto-mixer-off"  $LX/ff802-cc-auto-mixer-off
put 0755 "$PKGSRC/ff802-cc-notify"          $LX/ff802-cc-notify
put 0755 "$PKGSRC/ff802-cc-auto"            usr/bin/ff802-cc-auto
put 0644 "$PKGSRC/70-ff802-cc-auto.rules"   usr/lib/udev/rules.d/70-ff802-cc-auto.rules
put 0644 "$PKGSRC/ff802-cc-auto@.service"   usr/lib/systemd/system/ff802-cc-auto@.service
put 0644 "$PKGSRC/ff802-cc-notify.path"     usr/lib/systemd/user/ff802-cc-notify.path
put 0644 "$PKGSRC/ff802-cc-notify.service"  usr/lib/systemd/user/ff802-cc-notify.service
W=$STAGE/usr/lib/systemd/user/graphical-session.target.wants
mkdir -p "$W"
ln -s ../ff802-cc-notify.path "$W/ff802-cc-notify.path"
ln -s ../ff802-cc-notify.service "$W/ff802-cc-notify.service"
put 0644 "$PKGSRC/org.ff802cc.auto.policy"  usr/share/polkit-1/actions/org.ff802cc.auto.policy
put 0644 "$PKGSRC/copyright"                usr/share/doc/$NAME/copyright
put 0644 "$PKGSRC/README.Debian"            usr/share/doc/$NAME/README.Debian
gzip -n -9 -c "$PKGSRC/changelog" > "$STAGE/usr/share/doc/$NAME/changelog.gz"
chmod 0644 "$STAGE/usr/share/doc/$NAME/changelog.gz"

for s in postinst prerm postrm; do
    put 0755 "$PKGSRC/$s" "DEBIAN/$s"
done

(cd "$STAGE" && find usr -type f | LC_ALL=C sort | xargs md5sum) > "$STAGE/DEBIAN/md5sums"
chmod 0644 "$STAGE/DEBIAN/md5sums"

size=$(du -k -s --apparent-size "$STAGE/usr" | cut -f1)
sed -e "s/@VERSION@/$VERSION/" -e "s/@SIZE@/$size/" -e "s/@ARCH@/$ARCH/" \
    "$PKGSRC/control.in" > "$STAGE/DEBIAN/control"
chmod 0644 "$STAGE/DEBIAN/control"

find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

DEB=$OUT/${NAME}_${VERSION}_${ARCH}.deb
dpkg-deb --root-owner-group -Zxz -z9 --build "$STAGE" "$DEB" > /dev/null
rm -rf -- "${STAGE:?}"
echo "$DEB"
