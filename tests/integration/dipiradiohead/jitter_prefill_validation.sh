#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

require_itest_helper

# jitter-src: 1 s burst, then real time, one 2 s stall. longest audio gap in first 8 s (EOF flush excluded). PSI keeps flowing during the stall, and idle flushes send partial datagrams, so only full datagrams carrying audio pid 0x0101 count
FPB=$(free_port_block 7)
GROUP=$(unique_mcast 61)

run_phase() {
    name=$1
    http_port=$2
    mport=$3
    shift 3
    "$DVBIPI_ITEST_HELPER" jitter-src "$http_port" "$GROUP" "$mport" "$WORK/$name.res" &
    src_pid=$!
    sleep 0.5
    "$BIN" -i "http://127.0.0.1:$http_port/stream" -O lo -m "$GROUP:$mport" "$@" >"$WORK/$name.log" 2>&1 &
    tool_pid=$!
    wait "$src_pid" || fail "$name: helper failed"
    kill -INT "$tool_pid" 2>/dev/null
    wait "$tool_pid" 2>/dev/null
    return $?
}

run_phase control $((FPB + 0)) $((FPB + 1)) &
ctl=$!
run_phase jitter $((FPB + 2)) $((FPB + 3)) --jitter-ms 3000 &
jit=$!
wait "$ctl"
wait "$jit"

[ -s "$WORK/control.res" ] || fail "control: no result (see $WORK/control.log)"
[ -s "$WORK/jitter.res" ] || fail "jitter: no result (see $WORK/jitter.log)"

read -r ctl_n ctl_gap <"$WORK/control.res"
read -r jit_n jit_gap <"$WORK/jitter.res"

[ "$ctl_n" -gt 50 ] || fail "control: only $ctl_n datagrams"
[ "$jit_n" -gt 50 ] || fail "jitter: only $jit_n datagrams"

awk -v g="$ctl_gap" 'BEGIN { exit !(g > 1.0) }' || fail "control: expected stall gap > 1.0 s, got $ctl_gap s"
awk -v g="$jit_gap" 'BEGIN { exit !(g < 0.5) }' || fail "jitter: stall not bridged, longest gap $jit_gap s"

command -v ffmpeg >/dev/null 2>&1 || fail "'ffmpeg' not in PATH"

ffmpeg -loglevel error -f lavfi -i "sine=frequency=440:duration=12" -c:a aac -b:a 64k -f segment -segment_time 2 -segment_format mpegts "$WORK/seg%d.ts" || fail "ffmpeg failed"

{
    printf '#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n'
    for i in 0 1 2 3 4; do
        printf '#EXTINF:2.0,\nseg%d.ts\n' "$i"
    done
} >"$WORK/live.m3u8"

"$DVBIPI_ITEST_HELPER" httpd $((FPB + 4)) "$WORK" 2>"$WORK/hls_server.log" &
hls_pid=$!
sleep 0.5
"$BIN" -i "http://127.0.0.1:$((FPB + 4))/live.m3u8" -m $GROUP:$((FPB + 5)) --jitter-ms 1000 >"$WORK/hls.log" 2>&1 &
tool_pid=$!
sleep 5
kill -INT "$tool_pid" 2>/dev/null
wait "$tool_pid" 2>/dev/null
kill "$hls_pid" 2>/dev/null

assert_contains "$WORK/hls_server.log" "GET /seg" "HLS input with --jitter-ms fetches segments"

echo "OK"
