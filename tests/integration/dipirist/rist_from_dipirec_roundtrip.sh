#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# $1: dipirist binary (receiver). $2: dipirec binary (sender, -o rist://).
RIST_BIN=$1
REC_BIN=$2
BIN=$RIST_BIN
. "$(dirname "$0")/../common.sh"

if [ -z "${REC_BIN:-}" ] || [ ! -x "$REC_BIN" ]; then
    echo "FAIL: no dipirec binary given as \$2 ($REC_BIN)" >&2
    exit 1
fi

PORT=17966
N_PKTS=200

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

"$RIST_BIN" -i "rist://@0.0.0.0:$PORT" -o "$out" --buffer 200 >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$REC_BIN" -i "$fixture" -f raw -o "rist://127.0.0.1:$PORT" --buffer 200 >"$WORK/send.log" 2>&1
# file source hits EOF -> nonzero rc by this toolkit's convention, not a failure here

sleep 1
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true

cmp -s "$fixture" "$out" || fail "dipirec -o rist:// -> dipirist: round-tripped output differs from input (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
