#!/bin/sh
# Build the ff802-cc Debian package (ff802-cc and ff802-detect, release).
#
# Usage: packaging/build-deb-cc.sh <output dir> [<package version override>]
#
# - refuses (exit 3) if tracked files differ from HEAD (staged or not):
#   the package must correspond to exactly one commit; every file packaged
#   from packaging/ must be committed; other untracked files are not part
#   of the build and are only listed
# - configures a FRESH build directory <output dir>/.build-cc as Release
#   with FF802_ENABLE_USB_WRITE=ON, so CMake determines the Git revision of
#   this commit; verifies it against git rev-parse
# - builds only the targets ff802-cc and ff802-detect; the programs are
#   stripped and copied, never executed
# - package version and SOURCE_DATE_EPOCH come from
#   packaging/ff802-cc/changelog (the override is for upgrade tests only;
#   the project version compiled into the programs is not changed by it)
#
# The same commit built with the same tool versions gives a byte-identical
# .deb (fixed modes, root ownership, timestamps, gzip -n, sorted members).
# Needs: git, cmake, a C compiler, libusb-1.0-0-dev, dpkg-deb, gzip, md5sum.

set -eu

SRC=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PKGSRC=$SRC/packaging/ff802-cc
OUT=${1:?usage: build-deb-cc.sh <output dir> [<version>]}
NAME=ff802-cc

die() { echo "build-deb-cc: $1" >&2; exit "${2:-2}"; }

# --- one commit, nothing else -------------------------------------------------
command -v git > /dev/null 2>&1 || die "git not found"
git -C "$SRC" rev-parse --git-dir > /dev/null 2>&1 || die "$SRC is not a Git checkout"
# the checkout must be SRC itself, not a repository it happens to lie in
top=$(git -C "$SRC" rev-parse --show-toplevel 2>/dev/null || true)
[ -n "$top" ] && [ "$(CDPATH= cd -- "$top" && pwd -P)" = "$(CDPATH= cd -- "$SRC" && pwd -P)" ] ||
    die "$SRC is not a Git checkout (it lies inside '$top')"
changes=$(git -C "$SRC" status --porcelain --untracked-files=no)
if [ -n "$changes" ]; then
    echo "build-deb-cc: uncommitted changes in tracked files; commit or stash them first:" >&2
    printf '%s\n' "$changes" | sed 's/^/  /' >&2
    exit 3
fi
untracked=$(git -C "$SRC" status --porcelain --untracked-files=normal | sed -n 's/^?? //p')
[ -z "$untracked" ] || printf 'build-deb-cc: untracked (not part of the build): %s\n' \
    "$(printf '%s ' $untracked)" >&2
# every file this script packages must be part of the commit
for f in packaging/build-deb-cc.sh packaging/ff802-cc/changelog packaging/ff802-cc/control.in \
         packaging/ff802-cc/copyright packaging/ff802-cc/README.Debian; do
    git -C "$SRC" ls-files --error-unmatch -- "$f" > /dev/null 2>&1 ||
        die "$f is not committed; commit it first" 3
done
REV=$(git -C "$SRC" rev-parse --short=12 HEAD)
COMMIT=$(git -C "$SRC" rev-parse HEAD)

head=$(sed -n '1p' "$PKGSRC/changelog")
VERSION=$(printf '%s\n' "$head" | sed -n 's/^ff802-cc (\([^)]*\)).*/\1/p')
[ -n "$VERSION" ] || die "cannot read the version from changelog"
VERSION=${2:-$VERSION}
case $VERSION in
    *[!0-9A-Za-z.+~-]*|"") die "invalid version '$VERSION'" ;;
esac
trailer=$(sed -n 's/^ -- .*>  \(.*\)$/\1/p' "$PKGSRC/changelog" | head -n 1)
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(date -d "$trailer" +%s)}
export SOURCE_DATE_EPOCH
ARCH=$(dpkg --print-architecture)

mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)

# --- programs (compiled only, fresh build directory) -------------------------------
B=$OUT/.build-cc
rm -rf -- "${B:?}"
cmake -S "$SRC" -B "$B" -DCMAKE_BUILD_TYPE=Release -DFF802_WERROR=ON \
      -DBUILD_TESTING=OFF -DFF802_ENABLE_USB_WRITE=ON > "$B.configure.log" 2>&1 ||
    { cat "$B.configure.log" >&2; exit 1; }
detected=$(sed -n 's/^FF802_GIT_REVISION_DETECTED:INTERNAL=//p' "$B/CMakeCache.txt")
[ "$detected" = "$REV" ] ||
    die "CMake determined revision '$detected', expected '$REV'; not packaged" 1
PROJECT_VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$B/CMakeCache.txt")
cmake --build "$B" --target ff802-cc ff802-detect -j > "$B.build.log" 2>&1 ||
    { cat "$B.build.log" >&2; exit 1; }

# --- stage ----------------------------------------------------------------------
STAGE=$OUT/.stage-$NAME
rm -rf -- "${STAGE:?}"
umask 022
mkdir -p "$STAGE/DEBIAN"

put() { # put <mode> <source> <path in package>
    mkdir -p "$STAGE/$(dirname -- "$3")"
    cp -- "$2" "$STAGE/$3"
    chmod "$1" "$STAGE/$3"
}
put 0755 "$B/ff802-cc"     usr/bin/ff802-cc
put 0755 "$B/ff802-detect" usr/bin/ff802-detect
strip --strip-unneeded "$STAGE/usr/bin/ff802-cc" "$STAGE/usr/bin/ff802-detect"
DOC=usr/share/doc/$NAME
put 0644 "$PKGSRC/copyright"     $DOC/copyright
put 0644 "$PKGSRC/README.Debian" $DOC/README.Debian
gzip -n -9 -c "$PKGSRC/changelog" > "$STAGE/$DOC/changelog.gz"
chmod 0644 "$STAGE/$DOC/changelog.gz"
cc=$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$B/CMakeCache.txt")
{
    echo "package: $NAME $VERSION ($ARCH)"
    echo "project version: $PROJECT_VERSION"
    echo "git commit: $COMMIT"
    echo "git revision: $REV"
    echo "build type: Release, FF802_ENABLE_USB_WRITE=ON, FF802_WERROR=ON"
    echo "compiler: $("$cc" --version | head -n 1)"
    echo "cmake: $(cmake --version | head -n 1)"
    echo "libusb-1.0-0-dev: $(dpkg-query -W -f='${Version}' libusb-1.0-0-dev 2>/dev/null || echo unknown)"
    echo "SOURCE_DATE_EPOCH: $SOURCE_DATE_EPOCH"
} > "$STAGE/$DOC/BUILDINFO"
chmod 0644 "$STAGE/$DOC/BUILDINFO"

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
