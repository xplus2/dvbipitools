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

# feeds the sender through a fifo, data starts after the handshake had time to complete
# (librist drops data while the receiver's handshake is pending)
send_fixture() {
    sf_fifo="$WORK/send.fifo"
    rm -f "$sf_fifo"
    mkfifo "$sf_fifo"
    "$BIN" -i "$sf_fifo" -o "rist://127.0.0.1:$PORT" --buffer 200 >"$1" 2>&1 &
    sf_pid=$!
    exec 3>"$sf_fifo"
    sleep 1
    rm -f "$WORK"/slice.*
    split -b 18800 "$fixture" "$WORK/slice."
    for sf_slice in "$WORK"/slice.*; do
        cat "$sf_slice" >&3
        sleep 0.1
    done
    exec 3>&-
    # file source hits EOF -> nonzero rc by this toolkit's convention, not a failure here
    wait $sf_pid || true
    return 0
}

"$BIN" -i "rist://@0.0.0.0:$PORT" -o "$out" --buffer 200 >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

send_fixture "$WORK/send.log"

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
send_fixture "$WORK/send2.log"
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
