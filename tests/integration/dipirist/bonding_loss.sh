#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/bonding_common.sh"

skip_unless_bonding_testable

PORTA=17974
PORTB=17976
N_PKTS=1000

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"
make_fixture "$fixture" "$N_PKTS"

net_setup $PORTA loss 30%

"$BIN" -i "rist://@0.0.0.0:$PORTA" -i "rist://@0.0.0.0:$PORTB" -o "$out" --buffer 200 --profile main >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$BIN" -i "$fixture" -o "rist://127.0.0.1:$PORTA" -o "rist://127.0.0.1:$PORTB" --buffer 200 --profile main >"$WORK/send.log" 2>&1

sleep 2
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true
net_teardown

cmp -s "$fixture" "$out" || fail "dipirist: bonded round-trip did not survive 30% loss on one link (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
