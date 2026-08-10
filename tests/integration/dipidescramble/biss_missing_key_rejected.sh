#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v ffmpeg >/dev/null 2>&1 || fail "required tool 'ffmpeg' not found on PATH"

DIPITVHEAD=$(echo "$BIN" | sed 's#/dipidescramble\([^/]*\)$#/../dipitvhead/dipitvhead\1#')
[ -x "$DIPITVHEAD" ] || DIPITVHEAD="./dipitvhead"
[ -x "$DIPITVHEAD" ] || fail "cannot locate dipitvhead binary (tried $DIPITVHEAD)"

MCAST=239.255.7.47
PORT=17749
SW=00112233445566778899aabbccddeeff

out="$WORK/descrambled.ts"

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 6 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg.log" | \
timeout 8 "$DIPITVHEAD" -O lo -u -m $MCAST:$PORT -i - -s "BISS Test" \
    --biss2-sw "$SW" --cas-pids video,audio \
    >"$WORK/dipitvhead.log" 2>&1 &
TVHEADPID=$!
sleep 0.5

run_expect_rc 1 "dipidescramble without --biss2-sw" \
    sh -c "timeout 8 '$BIN' -i 'udp://@$MCAST:$PORT' -I lo -o '$out' -f ts >'$WORK/dipidescramble.log' 2>&1"

kill $TVHEADPID 2>/dev/null
wait $TVHEADPID 2>/dev/null

assert_contains "$WORK/dipidescramble.log" "BISS stream detected, no --biss1-sw/--biss2-sw/--biss2-esw given" "dipidescramble missing-key error"

echo "OK"

#EOF
