#!/bin/sh
# Release build with -Werror in two different build directories; the
# programs must be byte-identical (no build paths, timestamps or other
# varying data in the binaries).
#
# Usage: check_reproducible_build.sh <source dir> <scratch dir>

set -eu
SRC=$1
T=$2
case $T in
    */repro-scratch) ;;
    *) echo "refusing unexpected scratch dir: $T" >&2; exit 2 ;;
esac
rm -rf -- "${T:?}"
mkdir -p "$T"

for d in a b; do
    cmake -S "$SRC" -B "$T/$d" -DCMAKE_BUILD_TYPE=Release -DFF802_WERROR=ON \
          -DBUILD_TESTING=OFF > "$T/$d.configure.log" 2>&1 ||
        { cat "$T/$d.configure.log"; exit 1; }
    cmake --build "$T/$d" -j > "$T/$d.build.log" 2>&1 ||
        { cat "$T/$d.build.log"; exit 1; }
done

rc=0
n=0
for prog in ff802-detect ff802-cc ff802-chanmap; do
    [ -f "$T/a/$prog" ] || continue
    n=$((n + 1))
    if cmp -s "$T/a/$prog" "$T/b/$prog"; then
        echo "identical: $prog"
    else
        echo "DIFFERENT: $prog"
        rc=1
    fi
done
[ "$n" -gt 0 ] || { echo "no program built"; exit 1; }
exit $rc
