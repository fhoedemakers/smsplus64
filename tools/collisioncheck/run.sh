#!/bin/bash
# Build the collision checker for the host and run it over every rom in
# filesystem/. Exits non-zero if the collision-only pass ever disagrees with
# the drawing path. Nothing here is part of the Nintendo 64 build.
set -u
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FRAMES=${FRAMES:-20000}

gcc -O2 -DLSB_FIRST -Itools/collisioncheck -I. -Ismsplus \
    -o "$OUT/collisioncheck" \
    tools/collisioncheck/main.c \
    smsplus/render.c smsplus/vdp.c smsplus/sms.c smsplus/z80.c \
    smsplus/sn76496.c smsplus/system.c smsplus/loadrom.c -lm 2>&1 |
    grep -E "error|undefined" && { echo "build failed"; exit 1; }

status=0
shopt -s nullglob nocaseglob
for rom in filesystem/*.sms filesystem/*.gg; do
    "$OUT/collisioncheck" "$rom" "$FRAMES" | grep -vE "^Acquired cacheStore" || status=1
    echo
done
exit $status
