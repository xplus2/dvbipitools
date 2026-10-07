#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq python3; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

IN_GROUP=$(unique_mcast 61)
FPB=$(free_port_block 2)
IN_PORT=$((FPB + 0))
OUT_GROUP=$(unique_mcast 62)
OUT_PORT=$((FPB + 1))

clip="$WORK/clip.ts"
cap="$WORK/capture.ts"
report="$WORK/report.json"
rxlog="$WORK/receiver.log"

gen_test_clip "$clip" 1000 3

# every 5th adjacent RTP pair is sent swapped
cat >"$WORK/send.py" <<'EOF'
import socket, struct, sys, time

clip, group, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = open(clip, "rb").read()
pkts = [data[i:i + 7 * 188] for i in range(0, len(data) - 7 * 188 + 1, 7 * 188)]
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton("127.0.0.1"))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
frames = [struct.pack("!BBHII", 0x80, 33, (1000 + i) & 0xFFFF, i * 900, 0x1234) + p for i, p in enumerate(pkts)]
for i in range(0, len(frames) - 1, 5):
    frames[i], frames[i + 1] = frames[i + 1], frames[i]
for f in frames:
    s.sendto(f, (group, port))
    time.sleep(0.003)
EOF

tsp -I ip "$OUT_GROUP:$OUT_PORT" --local-address 127.0.0.1 --receive-timeout 6000 -O file "$cap" >"$WORK/tsp.log" 2>&1 &
tsp_pid=$!

"$BIN" -i "rtp://@$IN_GROUP:$IN_PORT" -I lo --jitter-ms 100 -O lo -u -m "$OUT_GROUP:$OUT_PORT" -s "Jitter Channel" >"$rxlog" 2>&1 &
rx_pid=$!
sleep 0.7
kill -0 "$rx_pid" 2>/dev/null || fail "receiver exited early (see $rxlog)"

python3 "$WORK/send.py" "$clip" "$IN_GROUP" "$IN_PORT" || fail "sender failed"
sleep 1.5
kill -INT "$rx_pid" 2>/dev/null
wait "$rx_pid" 2>/dev/null
wait "$tsp_pid" || true

[ -s "$cap" ] || fail "no packets captured (see $rxlog)"

assert_contains "$rxlog" "jitter buffer: reordered [1-9]" "reordering seen"
assert_contains "$rxlog" "lost 0 late 0 dup 0" "nothing lost or late"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "tsanalyze failed, see $WORK/tsanalyze.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "$cc_errors continuity-counter errors in capture"

service_name=$(jq -r '.services[0].name // empty' "$report")
[ "$service_name" = "Jitter Channel" ] || fail "expected service name 'Jitter Channel', got '$service_name'"

echo "OK"
