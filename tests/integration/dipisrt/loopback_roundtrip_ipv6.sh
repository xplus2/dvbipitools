#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

PORT=17976
N_PKTS=200

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"

: > "$fixture"
i=0
while [ "$i" -lt "$N_PKTS" ]; do
    printf '\107' >> "$fixture"
    dd if=/dev/urandom bs=187 count=1 2>/dev/null >> "$fixture"
    i=$((i + 1))
done

"$BIN" -i "srt://@[::1]:$PORT" -o "$out" >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$BIN" -i "$fixture" -o "srt://[::1]:$PORT" >"$WORK/send.log" 2>&1

sleep 2
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true

cmp -s "$fixture" "$out" || fail "dipisrt: IPv6 round-tripped output differs from input (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
