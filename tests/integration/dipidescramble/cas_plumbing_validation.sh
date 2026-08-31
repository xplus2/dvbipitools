#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsecmg openssl nc; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

wait_port() {
    i=0
    while [ $i -lt 50 ]; do
        nc -z 127.0.0.1 "$1" >/dev/null 2>&1 && return 0
        i=$((i + 1))
        sleep 0.1
    done
    return 1
}

DIPITVHEAD=$(echo "$BIN" | sed 's#/dipidescramble\([^/]*\)$#/../dipitvhead/dipitvhead\1#')
[ -x "$DIPITVHEAD" ] || DIPITVHEAD="./dipitvhead"
[ -x "$DIPITVHEAD" ] || fail "cannot locate dipitvhead binary (tried $DIPITVHEAD)"

MCAST=239.255.7.43
PORT=17743
ECMG_PORT=12243
EMMG_PORT=18008

key="$WORK/testkey.pem"
emm="$WORK/emm_cache.bin"
out="$WORK/descrambled.ts"

openssl genrsa -out "$key" 2048 >/dev/null 2>&1

tsecmg -p $ECMG_PORT -s >"$WORK/tsecmg.log" 2>&1 &
ECMGPID=$!
wait_port $ECMG_PORT || fail "tsecmg never started listening on $ECMG_PORT (see $WORK/tsecmg.log)"

"$BIN" -i "udp://@$MCAST:$PORT" -I lo -k "$key" -s deadbeef -e "$emm" \
    -o "$out" -f ts >"$WORK/dipidescramble.log" 2>&1 &
DESCPID=$!
sleep 0.3

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 6 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg.log" | \
timeout 8 "$DIPITVHEAD" -O lo -u -m $MCAST:$PORT -i - -s "CAS Test" \
    --cas-algo cissa --cas-ecmg "tcp://127.0.0.1:$ECMG_PORT" --cas-ecmg-version 2 \
    --cas-emmg-port $EMMG_PORT --cas-super-id 0x4A750002 --cas-ecm-id 1 --cas-pids video,audio \
    --cas-cp-duration 3000 \
    >"$WORK/dipitvhead.log" 2>&1

sleep 1
kill $DESCPID 2>/dev/null
kill $ECMGPID 2>/dev/null
wait $DESCPID 2>/dev/null

assert_not_contains "$WORK/dipidescramble.log" "cannot load RSA private key" "dipidescramble startup"

assert_contains "$WORK/dipidescramble.log" "CAS parameters resolved" "dipidescramble CAT/PMT parsing"

[ -s "$out" ] || fail "dipidescramble: no output file produced"

echo "OK"

#EOF
