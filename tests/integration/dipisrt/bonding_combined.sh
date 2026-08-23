#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/bonding_common.sh"

skip_unless_bonding_testable

PORTA=18000
PORTB=18001
N_PKTS=6000
PASSPHRASE="torturecombinedscenario1"
FEC="fec,cols:10,rows:5"

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"
make_fixture "$fixture" "$N_PKTS"

# start for handshake
net_setup2 $PORTA "loss 0% delay 10ms" $PORTB "loss 0% delay 10ms"

LATENCY=400

"$BIN" -i "srt://@0.0.0.0:$PORTA" -i "srt://@0.0.0.0:$PORTB" -o "$out" \
    --group-mode broadcast --passphrase "$PASSPHRASE" --packetfilter "$FEC" --latency $LATENCY \
    >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

(
    "$BIN" -i "$fixture" -o "srt://127.0.0.1:$PORTA" -o "srt://127.0.0.1:$PORTB" \
        --group-mode broadcast --passphrase "$PASSPHRASE" --packetfilter "$FEC" --latency $LATENCY \
        >"$WORK/send.log" 2>&1
) &
SENDPID=$!

sleep 1.5

# effective loss = p_A*p_B = 5%
net_change2 A "loss 25% delay 20ms 10ms"
net_change2 B "loss 20% delay 40ms 20ms 15% reorder 30% 20%"
sleep 8

# B carries traffic at 10% loss, under fec's 30% parity.
# A: 95%, avoids libsrt permanently excluding silent members.
net_change2 A "loss 95%"
net_change2 B "loss 10% delay 30ms 15ms"
sleep 3

# both links alive before next blackout
net_change2 A "loss 25% delay 20ms 10ms"
net_change2 B "loss 20% delay 40ms 20ms 15% reorder 30% 20%"
sleep 2

# B: 95%, same reasoning.
net_change2 A "loss 10% delay 20ms 10ms"
net_change2 B "loss 95%"
sleep 3

net_change2 A "loss 25% delay 20ms 10ms"
net_change2 B "loss 20% delay 40ms 20ms 15% reorder 30% 20%"
sleep 4

wait $SENDPID 2>/dev/null || true
sleep 1
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true
net_teardown

cmp -s "$fixture" "$out" || fail "dipisrt: broadcast+crypto+FEC did not survive both bonded links impaired concurrently (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
