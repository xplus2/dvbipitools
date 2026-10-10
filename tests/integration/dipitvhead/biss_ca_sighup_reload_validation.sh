#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze openssl jq ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 1)
PORT=$((FPB + 0))

keys="$WORK/receivers"
cap="$WORK/capture.ts"
fifo="$WORK/ts.fifo"
log="$WORK/dipitvhead.log"
mkdir "$keys"

new_key() {
    key_name=$1
    openssl genrsa 2048 2>/dev/null | openssl rsa -pubout -out "$keys/$key_name.pem" 2>/dev/null \
        || fail "could not generate receiver key $key_name"
    return $?
}

new_key r1
new_key r2

mkfifo "$fifo"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 60000 \
    -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!
wait_until 30 udp_port_busy $PORT || fail "biss-ca: tsp capture never bound $PORT"

ffmpeg -hide_banner -loglevel error -y -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 70 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts "$fifo" 2>"$WORK/ffmpeg.log" &
FFPID=$!

"$BIN" -O lo -u -m $MCAST:$PORT -i - -s "Reload Test" --biss2-ca-receivers "$keys" \
    >"$log" 2>&1 <"$fifo" &
TVPID=$!

wait_until 60 log_has "$log" "loaded 2 entitled receiver" || fail "biss-ca: initial receiver load not logged (see $log)"
kill -0 "$TVPID" 2>/dev/null || fail "biss-ca: dipitvhead exited early (see $log)"

new_key r3
kill -HUP "$TVPID"

wait_until 60 log_has "$log" "loaded 3 entitled receiver" || fail "biss-ca: SIGHUP did not reload the receiver directory (see $log)"

rm -f "$keys/r1.pem" "$keys/r2.pem" "$keys/r3.pem"
kill -HUP "$TVPID"
empty_reported() {
    grep -q "reload of .* produced zero usable receivers" "$log"
}
wait_until 60 empty_reported || fail "biss-ca: emptied receiver directory was not reported (see $log)"
kill -0 "$TVPID" 2>/dev/null || fail "biss-ca: dipitvhead died after an empty reload (see $log)"

sleep 1
kill $FFPID $TVPID 2>/dev/null
wait $FFPID $TVPID 2>/dev/null || true
kill $TSPID 2>/dev/null
wait $TSPID 2>/dev/null || true

[ -s "$cap" ] || fail "biss-ca: no packets captured (see $log)"
tsanalyze --json "$cap" >"$WORK/report.json" 2>"$WORK/tsanalyze.log" \
    || fail "biss-ca: tsanalyze failed, see $WORK/tsanalyze.log"
is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/report.json")
[ "$is_scrambled" = "true" ] || fail "biss-ca: expected scrambled output, is-scrambled=$is_scrambled"

echo "OK"

#EOF
