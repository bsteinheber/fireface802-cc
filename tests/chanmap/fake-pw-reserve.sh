#!/bin/sh
# Stand-in for pw-reserve: records its arguments and process group, holds
# the "reservation" until SIGINT, then records the release and exits 0.
# FAKE_RESERVE_DIR: where to write; FAKE_RESERVE_IGNORE_INT=1: ignore SIGINT.
d=${FAKE_RESERVE_DIR:?}
echo "$*" > "$d/args"
awk '{print $5}' /proc/$$/stat > "$d/pgid"
if [ "${FAKE_RESERVE_IGNORE_INT:-}" = 1 ]; then
    trap '' INT
else
    trap 'echo released > "$d/released"; exit 0' INT
fi
echo acquired > "$d/acquired"
while :; do sleep 0.05; done
