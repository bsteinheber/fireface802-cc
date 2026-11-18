#!/bin/sh
# Build the ff802-ucm-profile Debian package (reproducible).
#
# Usage: packaging/build-deb.sh <output dir> [<version override>]
#
# Version and SOURCE_DATE_EPOCH come from packaging/ff802-ucm/changelog
# (first entry). A version override is only meant for upgrade tests.
# The same sources give a byte-identical .deb: fixed file modes, root
# ownership, all timestamps set to SOURCE_DATE_EPOCH, gzip -n, sorted
# archive members. Needs: dpkg-deb, gzip, md5sum, date.

set -eu

SRC=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PKGSRC=$SRC/packaging/ff802-ucm
OUT=${1:?usage: build-deb.sh <output dir> [<version>]}
NAME=ff802-ucm-profile

head=$(sed -n '1p' "$PKGSRC/changelog")
VERSION=$(printf '%s\n' "$head" | sed -n 's/^ff802-ucm-profile (\([^)]*\)).*/\1/p')
[ -n "$VERSION" ] || { echo "build-deb: cannot read the version from changelog" >&2; exit 2; }
VERSION=${2:-$VERSION}
case $VERSION in
    *[!0-9A-Za-z.+~-]*|"") echo "build-deb: invalid version '$VERSION'" >&2; exit 2 ;;
esac
trailer=$(sed -n 's/^ -- .*>  \(.*\)$/\1/p' "$PKGSRC/changelog" | head -n 1)
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(date -d "$trailer" +%s)}
export SOURCE_DATE_EPOCH

mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
STAGE=$OUT/.stage-$NAME
rm -rf -- "${STAGE:?}"
umask 022
mkdir -p "$STAGE/DEBIAN"

put() { # put <mode> <source> <path in package>
    mkdir -p "$STAGE/$(dirname -- "$3")"
    cp -- "$2" "$STAGE/$3"
    chmod "$1" "$STAGE/$3"
}

UCM=$SRC/audio/ucm2/USB-Audio
put 0644 "$UCM/conf.d/0424-3fdd.conf"         usr/share/alsa/ucm2/USB-Audio/conf.d/0424-3fdd.conf
put 0644 "$UCM/RME/Fireface-802.conf"         usr/share/alsa/ucm2/USB-Audio/RME/Fireface-802.conf
put 0644 "$UCM/RME/Fireface-802-HiFi.conf"    usr/share/alsa/ucm2/USB-Audio/RME/Fireface-802-HiFi.conf
put 0644 "$PKGSRC/longname-master.conf"       usr/share/ff802-ucm/longname-master.conf
put 0755 "$PKGSRC/ff802-ucm-bind"             usr/sbin/ff802-ucm-bind
put 0644 "$PKGSRC/77-ff802-ucm-bind.rules"    usr/lib/udev/rules.d/77-ff802-ucm-bind.rules
sed -i "s/@VERSION@/$VERSION/" "$STAGE/usr/sbin/ff802-ucm-bind"
put 0644 "$PKGSRC/copyright"                  usr/share/doc/$NAME/copyright
put 0644 "$PKGSRC/README.Debian"              usr/share/doc/$NAME/README.Debian
gzip -n -9 -c "$PKGSRC/changelog" > "$STAGE/usr/share/doc/$NAME/changelog.gz"
chmod 0644 "$STAGE/usr/share/doc/$NAME/changelog.gz"

for s in postinst prerm postrm; do
    put 0755 "$PKGSRC/$s" "DEBIAN/$s"
done

(cd "$STAGE" && find usr -type f | LC_ALL=C sort | xargs md5sum) > "$STAGE/DEBIAN/md5sums"
chmod 0644 "$STAGE/DEBIAN/md5sums"

size=$(du -k -s --apparent-size "$STAGE/usr" | cut -f1)
sed -e "s/@VERSION@/$VERSION/" -e "s/@SIZE@/$size/" "$PKGSRC/control.in" > "$STAGE/DEBIAN/control"
chmod 0644 "$STAGE/DEBIAN/control"

find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

DEB=$OUT/${NAME}_${VERSION}_all.deb
dpkg-deb --root-owner-group -Zxz -z9 --build "$STAGE" "$DEB" > /dev/null
rm -rf -- "${STAGE:?}"
echo "$DEB"
