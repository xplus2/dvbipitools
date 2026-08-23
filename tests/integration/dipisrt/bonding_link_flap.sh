#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/bonding_common.sh"

skip_unless_bonding_testable

PORTA=17994
PORTB=17995
N_PKTS=1500

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"
make_fixture "$fixture" "$N_PKTS"

net_setup $PORTA loss 0%

"$BIN" -i "srt://@0.0.0.0:$PORTA" -i "srt://@0.0.0.0:$PORTB" -o "$out" --group-mode backup >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

(
    "$BIN" -i "$fixture" -o "srt://127.0.0.1:$PORTA" -o "srt://127.0.0.1:$PORTB" --group-mode backup >"$WORK/send.log" 2>&1
) &
SENDPID=$!

# flap link A a few times while the transfer is in flight: fully down, then clean again
i=0
while [ "$i" -lt 4 ]; do
    sleep 0.3
    net_change $PORTA loss 100%
    sleep 0.3
    net_change $PORTA loss 0%
    i=$((i + 1))
done

wait $SENDPID 2>/dev/null || true
sleep 1
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true
net_teardown

cmp -s "$fixture" "$out" || fail "dipisrt: backup mode did not survive link A flapping (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
