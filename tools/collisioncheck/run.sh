#!/bin/bash
# Build the collision checker for the host and run it over every rom in
# filesystem/. Exits non-zero if the collision-only pass ever disagrees with
# the drawing path. Nothing here is part of the Nintendo 64 build.
#
# SGROMS=<dir> also runs every .sg rom found below that directory.
# ASAN=1 builds with AddressSanitizer, which catches reads past a rom buffer.
set -u
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FRAMES=${FRAMES:-20000}
SAN=
[ "${ASAN:-0}" = 1 ] && SAN="-g -fsanitize=address -fno-omit-frame-pointer"

gcc -O2 $SAN -DLSB_FIRST -DSPR_LIST_CHECK -Itools/collisioncheck -I. -Ismsplus \
    -o "$OUT/collisioncheck" \
    tools/collisioncheck/main.c \
    smsplus/render.c smsplus/tms.c smsplus/vdp.c smsplus/sms.c smsplus/z80.c \
    smsplus/sn76496.c smsplus/system.c smsplus/loadrom.c -lm 2>&1 |
    grep -E "error|undefined" && { echo "build failed"; exit 1; }

roms=()
shopt -s nullglob nocaseglob
roms+=(filesystem/*.sms filesystem/*.gg filesystem/*.sg)
if [ -n "${SGROMS:-}" ]; then
    while IFS= read -r -d '' rom; do roms+=("$rom"); done \
        < <(find "$SGROMS" -type f -iname '*.sg' -print0 | sort -z)
fi

status=0
for rom in "${roms[@]}"; do
    "$OUT/collisioncheck" "$rom" "$FRAMES" | grep -vE "^Acquired cacheStore|^SG: RAM adaptor"
    [ "${PIPESTATUS[0]}" = 0 ] || status=1
    echo
done
exit $status
