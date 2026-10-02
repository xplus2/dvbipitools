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

[ -w /dev/full ] || fail "/dev/full not writable here"

"$BIN" -i - -o /dev/full <"$clip" >"$WORK/dipidescramble.log" 2>&1
rc=$?
[ "$rc" = "1" ] || fail "expected exit 1 on output write failure, got $rc (see $WORK/dipidescramble.log)"

echo "OK"

#EOF
