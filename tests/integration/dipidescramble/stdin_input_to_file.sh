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

out="$WORK/out.ts"

"$BIN" -i - -o "$out" <"$clip" >"$WORK/dipidescramble.log" 2>&1
rc=$?
[ "$rc" = "0" ] || fail "expected exit 0 at end of input, got $rc (see $WORK/dipidescramble.log)"
in_size=$(wc -c <"$clip")
out_size=$(wc -c <"$out")
[ "$out_size" -gt $((in_size * 8 / 10)) ] || fail "output is only $out_size of $in_size bytes"
[ $((out_size % 188)) = "0" ] || fail "output is not whole TS packets"
tail -c "$out_size" "$clip" | cmp -s - "$out" || fail "clear stream was not passed through unchanged after sync"

echo "OK"

#EOF
