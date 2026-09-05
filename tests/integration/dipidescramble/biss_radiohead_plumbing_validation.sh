#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

DIPIRADIOHEAD=$(echo "$BIN" | sed 's#/dipidescramble\([^/]*\)$#/../dipiradiohead/dipiradiohead\1#')
[ -x "$DIPIRADIOHEAD" ] || DIPIRADIOHEAD="./dipiradiohead"
[ -x "$DIPIRADIOHEAD" ] || fail "cannot locate dipiradiohead binary (tried $DIPIRADIOHEAD)"

MCAST=239.255.7.56
PORT=17756
HTTP_PORT=18084
SW=00112233445566778899aabbccddeeff

ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=8" \
    -c:a libmp3lame -f mp3 "$WORK/stream.mp3"

ffmpeg -hide_banner -loglevel error -re -i "$WORK/stream.mp3" -c copy -f mp3 \
    -listen 1 "http://127.0.0.1:$HTTP_PORT/stream.mp3" \
    >"$WORK/ratesrv.log" 2>&1 &
FFSERVE_PID=$!
i=0
while ! ss -ltn 2>/dev/null | awk '{print $4}' | grep -q ":$HTTP_PORT\$"; do
    i=$((i + 1))
    [ "$i" -lt 200 ] || fail "biss-radiohead: paced http source on $HTTP_PORT never became ready"
    sleep 0.1
done

out="$WORK/descrambled.ts"

"$BIN" -i "udp://@$MCAST:$PORT" -I lo --biss2-sw "$SW" \
    -o "$out" -f ts >"$WORK/dipidescramble.log" 2>&1 &
DESCPID=$!
sleep 0.3

timeout 12 "$DIPIRADIOHEAD" -I lo -m $MCAST:$PORT -i "http://127.0.0.1:$HTTP_PORT/stream.mp3" -s "BISS Radio Test" \
    --biss2-sw "$SW" \
    >"$WORK/dipiradiohead.log" 2>&1 || true

sleep 1
kill $DESCPID $FFSERVE_PID 2>/dev/null
wait $DESCPID 2>/dev/null

assert_not_contains "$WORK/dipidescramble.log" "cannot load RSA private key" "dipidescramble startup"

assert_contains "$WORK/dipidescramble.log" "BISS Mode 1/E detected" "dipidescramble BISS detection"

[ -s "$out" ] || fail "dipidescramble: no output file produced"

echo "OK"

#EOF
