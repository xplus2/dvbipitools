#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg ffprobe; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

clip="$WORK/clip.ts"
gen_test_clip "$clip" 1000 3

mpts="$WORK/mpts.ts"

ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=320x240:rate=25" -f lavfi -i "sine=frequency=500" \
    -f lavfi -i "testsrc2=size=320x240:rate=25" -f lavfi -i "sine=frequency=900" -t 3 \
    -map 0:v -map 1:a -map 2:v -map 3:a -c:v libx264 -preset ultrafast -c:a aac \
    -program title=One:st=0:st=1 -program title=Two:st=2:st=3 -f mpegts "$mpts" || fail "cannot build the test MPTS"

"$BIN" -i - -o "$WORK/none.ts" <"$mpts" >"$WORK/auto.log" 2>&1
rc=$?
[ "$rc" = "1" ] || fail "MPTS without -p: expected exit 1, got $rc"
assert_contains "$WORK/auto.log" "MPTS source, pick one" "auto selection refusal"
pids=$(sed -n 's/.*pmt_pid=0x\([0-9a-f]*\).*/\1/p' "$WORK/auto.log")
set -- $pids
[ "$#" = "2" ] || fail "expected 2 listed programs, got '$pids'"

"$BIN" -i - -p "0x$1" -o "$WORK/one.ts" <"$mpts" >"$WORK/one.log" 2>&1 || fail "-p 0x$1 failed (see $WORK/one.log)"
[ -s "$WORK/one.ts" ] || fail "-p 0x$1 wrote nothing"

"$BIN" -i - -p all -o "$WORK/all.ts" <"$mpts" >"$WORK/all.log" 2>&1 || fail "-p all failed (see $WORK/all.log)"
all_size=$(wc -c <"$WORK/all.ts")
[ "$all_size" -gt 0 ] || fail "-p all wrote nothing"
tail -c "$all_size" "$mpts" | cmp -s - "$WORK/all.ts" || fail "-p all did not pass the MPTS through unchanged after the probe"

"$BIN" -i - -p 0x0fff -o "$WORK/bad.ts" <"$mpts" >"$WORK/bad.log" 2>&1
[ "$?" = "1" ] || fail "-p with an unknown pid should exit 1"
assert_contains "$WORK/bad.log" "not found in this MPTS" "unknown pid"

"$BIN" -i - -p all -f mkv -o "$WORK/x.mkv" <"$mpts" >"$WORK/allmkv.log" 2>&1
[ "$?" = "1" ] || fail "-p all with -f mkv should exit 1"
assert_contains "$WORK/allmkv.log" "can't hold multiple programs" "mkv with all programs"

echo "OK"

#EOF
