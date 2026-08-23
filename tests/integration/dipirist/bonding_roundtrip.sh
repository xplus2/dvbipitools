#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/bonding_common.sh"

skip_unless_bonding_testable

PORTA=17970
PORTB=17972
N_PKTS=200

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"
make_fixture "$fixture" "$N_PKTS"

"$BIN" -i "rist://@0.0.0.0:$PORTA" -i "rist://@0.0.0.0:$PORTB" -o "$out" --buffer 200 --profile main >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$BIN" -i "$fixture" -o "rist://127.0.0.1:$PORTA" -o "rist://127.0.0.1:$PORTB" --buffer 200 --profile main >"$WORK/send.log" 2>&1

sleep 1
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true

cmp -s "$fixture" "$out" || fail "dipirist: bonded round-trip output differs from input, no impairment (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
