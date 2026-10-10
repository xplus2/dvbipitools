#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

PORT=$(free_udp_port)
N_PKTS=5000

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"

: > "$fixture"
i=0
while [ "$i" -lt "$N_PKTS" ]; do
    printf '\107' >> "$fixture"
    dd if=/dev/urandom bs=187 count=1 2>/dev/null >> "$fixture"
    i=$((i + 1))
done

"$BIN" -i "srt://@0.0.0.0:$PORT" -o "$out" >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$BIN" -i "$fixture" -o "srt://127.0.0.1:$PORT" >"$WORK/send.log" 2>&1

sleep 2

cmp -s "$fixture" "$out" || fail "dipisrt: round-tripped output differs from input (see $WORK/send.log, $WORK/recv.log)"

size1=$(wc -c < "$out")

# sender restart, receiver kept up
"$BIN" -i "$fixture" -o "srt://127.0.0.1:$PORT" >"$WORK/send2.log" 2>&1
sleep 2
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true

cat "$fixture" "$fixture" > "$WORK/expect2.ts"
cmp -s "$WORK/expect2.ts" "$out" || fail "dipisrt: output after sender restart differs from two concatenated inputs (first run $size1 B, see $WORK/send2.log, $WORK/recv.log)"

echo "OK"

#EOF
