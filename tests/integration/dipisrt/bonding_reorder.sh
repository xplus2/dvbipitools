#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/bonding_common.sh"

skip_unless_bonding_testable

PORTA=17992
PORTB=17993
N_PKTS=1000

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"
make_fixture "$fixture" "$N_PKTS"

net_setup $PORTA delay 10ms reorder 90% 50%

"$BIN" -i "srt://@0.0.0.0:$PORTA" -i "srt://@0.0.0.0:$PORTB" -o "$out" --group-mode broadcast >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$BIN" -i "$fixture" -o "srt://127.0.0.1:$PORTA" -o "srt://127.0.0.1:$PORTB" --group-mode broadcast >"$WORK/send.log" 2>&1

sleep 2
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true
net_teardown

cmp -s "$fixture" "$out" || fail "dipisrt: broadcast did not survive heavy reorder on one bonded link (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
