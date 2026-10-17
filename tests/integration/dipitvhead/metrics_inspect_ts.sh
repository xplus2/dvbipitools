#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v ffmpeg >/dev/null 2>&1 || fail "required tool 'ffmpeg' not found on PATH"

MCAST=$(unique_mcast 65)
FPB=$(free_port_block 3)

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 4 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg.log" | \
timeout 20 "$BIN" -O lo -u -m $MCAST:$FPB -i - -s "Inspect Test" \
    --metrics "$WORK/metrics.sock" --metrics-id tv --metrics-interval 1 \
    --metrics-inspect-ts full --metrics-inspect-ts-pids 0x100 --pcr-mode regenerate -b 3000 -S >"$WORK/dipitvhead.log" 2>&1

assert_contains "$WORK/dipitvhead.log" "locked: program 1" "dipitvhead input lock with inspection on"

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 3 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg2.log" | \
timeout 20 "$BIN" -O lo -m $MCAST:$FPB -i - -s "Rtp Fec Test" --al-fec 2:2 --al-fec-port $((FPB + 2)) \
    >"$WORK/dipitvhead_rtp.log" 2>&1

assert_contains "$WORK/dipitvhead_rtp.log" "locked: program 1" "dipitvhead rtp output with fec"

echo "OK"

#EOF
