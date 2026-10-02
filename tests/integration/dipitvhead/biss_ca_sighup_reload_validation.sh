#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze openssl jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.41.40
PORT=41041

keys="$WORK/receivers"
cap="$WORK/capture.ts"
fifo="$WORK/ts.fifo"
log="$WORK/dipitvhead.log"
mkdir "$keys"

new_key() {
    openssl genrsa 2048 2>/dev/null | openssl rsa -pubout -out "$keys/$1.pem" 2>/dev/null \
        || fail "could not generate receiver key $1"
}

new_key r1
new_key r2

mkfifo "$fifo"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 6000 \
    -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!

ffmpeg -hide_banner -loglevel error -y -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 5 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts "$fifo" 2>"$WORK/ffmpeg.log" &
FFPID=$!

"$BIN" -O lo -u -m $MCAST:$PORT -i - -s "Reload Test" --biss2-ca-receivers "$keys" \
    >"$log" 2>&1 <"$fifo" &
TVPID=$!

tries=0
until grep -q "loaded 2 entitled receiver" "$log"; do
    tries=$((tries + 1))
    [ "$tries" -le 50 ] || fail "biss-ca: initial receiver load not logged (see $log)"
    kill -0 "$TVPID" 2>/dev/null || fail "biss-ca: dipitvhead exited early (see $log)"
    sleep 0.1
done

new_key r3
kill -HUP "$TVPID"

tries=0
until grep -q "loaded 3 entitled receiver" "$log"; do
    tries=$((tries + 1))
    [ "$tries" -le 50 ] || fail "biss-ca: SIGHUP did not reload the receiver directory (see $log)"
    sleep 0.1
done

rm -f "$keys/r1.pem" "$keys/r2.pem" "$keys/r3.pem"
kill -HUP "$TVPID"
tries=0
until grep -q "reload of .* produced zero usable receivers" "$log"; do
    tries=$((tries + 1))
    [ "$tries" -le 50 ] || fail "biss-ca: emptied receiver directory was not reported (see $log)"
    sleep 0.1
done
kill -0 "$TVPID" 2>/dev/null || fail "biss-ca: dipitvhead died after an empty reload (see $log)"

wait $FFPID || true
wait $TVPID || true
wait $TSPID || true

[ -s "$cap" ] || fail "biss-ca: no packets captured (see $log)"
tsanalyze --json "$cap" >"$WORK/report.json" 2>"$WORK/tsanalyze.log" \
    || fail "biss-ca: tsanalyze failed, see $WORK/tsanalyze.log"
is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/report.json")
[ "$is_scrambled" = "true" ] || fail "biss-ca: expected scrambled output, is-scrambled=$is_scrambled"

echo "OK"

#EOF
