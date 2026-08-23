#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

PORTA=17974
PORTB=17975
N_PKTS=200

# bonding needs libsrt -DENABLE_BONDING=ON, off by default upstream/most distro packages.
# probe: doomed-fast group-mode call. gate active: rejects outright. no gate: srtout.c's
# own "group create failed". anything else: bonding available.
probe=$("$BIN" -i /dev/null -o "srt://127.0.0.1:1" -o "srt://127.0.0.1:2" --group-mode broadcast 2>&1)
case "$probe" in
    *"needs a libsrt built with bonding support"*|*"group create failed"*)
        echo "SKIP: this libsrt build has no bonding support (ENABLE_BONDING=OFF)"
        exit 0
        ;;
esac

fixture="$WORK/fixture.ts"
out="$WORK/out.ts"

: > "$fixture"
i=0
while [ "$i" -lt "$N_PKTS" ]; do
    printf '\107' >> "$fixture"
    dd if=/dev/urandom bs=187 count=1 2>/dev/null >> "$fixture"
    i=$((i + 1))
done

"$BIN" -i "srt://@0.0.0.0:$PORTA" -i "srt://@0.0.0.0:$PORTB" -o "$out" --group-mode broadcast >"$WORK/recv.log" 2>&1 &
RECPID=$!
sleep 0.5

"$BIN" -i "$fixture" -o "srt://127.0.0.1:$PORTA" -o "srt://127.0.0.1:$PORTB" --group-mode broadcast >"$WORK/send.log" 2>&1

sleep 2
kill -INT $RECPID 2>/dev/null
wait $RECPID 2>/dev/null || true

cmp -s "$fixture" "$out" || fail "dipisrt: bonded round-tripped output differs from input (see $WORK/send.log, $WORK/recv.log)"

echo "OK"

#EOF
