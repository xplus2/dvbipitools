#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

PORT=$(free_udp_port_pair)
N_PKTS=5000

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"

# sync-byte-aligned TS-like fixture, so dipirist's raw-vs-RTP auto-detect locks onto "raw"
: > "$fixture"
i=0
while [ "$i" -lt "$N_PKTS" ]; do
    printf '\107' >> "$fixture"
    dd if=/dev/urandom bs=187 count=1 2>/dev/null >> "$fixture"
    i=$((i + 1))
done

"$BIN" -i "rist://@0.0.0.0:$PORT" -o "$out" --buffer 200 >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$BIN" -i "$fixture" -o "rist://127.0.0.1:$PORT" --buffer 200 >"$WORK/send.log" 2>&1
# file source hits EOF -> nonzero rc by this toolkit's convention, not a failure here

want1=$((N_PKTS * 188))
first_complete() { [ "$(wc -c < "$out")" -ge "$want1" ]; }
wait_until 10 first_complete

if ! cmp -s "$fixture" "$out"; then
    echo "expected $want1 B, got $(wc -c < "$out") B" >&2
    cmp "$fixture" "$out" >&2
    for l in send recv; do
        echo "--- $l.log (tail) ---" >&2
        tail -n 30 "$WORK/$l.log" >&2
    done
    fail "dipirist: round-tripped output differs from input"
fi

size1=$(wc -c < "$out")

sleep 3

# sender restart, receiver kept up
"$BIN" -i "$fixture" -o "rist://127.0.0.1:$PORT" --buffer 200 >"$WORK/send2.log" 2>&1
out_complete() { [ "$(wc -c < "$out")" -ge "$((size1 * 2))" ]; return $?; }
wait_until 15 out_complete
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true

cat "$fixture" "$fixture" > "$WORK/expect2.ts"
if ! cmp -s "$WORK/expect2.ts" "$out"; then
    echo "expected $(wc -c < "$WORK/expect2.ts") B, got $(wc -c < "$out") B, first run $size1 B" >&2
    cmp "$WORK/expect2.ts" "$out" >&2
    for l in send send2 recv; do
        echo "--- $l.log (tail) ---" >&2
        tail -n 30 "$WORK/$l.log" >&2
    done
    fail "dipirist: output after sender restart differs from two concatenated inputs"
fi

echo "OK"

#EOF
