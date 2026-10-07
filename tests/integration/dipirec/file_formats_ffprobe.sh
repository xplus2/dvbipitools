#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg ffprobe; do
    command -v "$t" >/dev/null 2>&1 || skip "'$t' not found on PATH"
done

clip="$WORK/clip.ts"
gen_test_clip "$clip" 1000 3 || fail "could not generate test clip"

check_format() {
    fmt=$1
    want=$2
    out="$WORK/out.$fmt"

    "$BIN" -i "$clip" -o "$out" -f "$fmt" >"$WORK/rec_$fmt.log" 2>&1
    rc=$?
    [ "$rc" = "0" ] || fail "$fmt: expected exit 0, got $rc (see $WORK/rec_$fmt.log)"
    [ -s "$out" ] || fail "$fmt: empty output"
    ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$out" >"$WORK/probe_$fmt.txt" 2>"$WORK/probe_$fmt.err"
    [ -s "$WORK/probe_$fmt.err" ] && fail "$fmt: ffprobe complained: $(cat "$WORK/probe_$fmt.err")"
    for type in $want; do
        grep -qx "$type" "$WORK/probe_$fmt.txt" || fail "$fmt: no $type stream"
    done
    return 0
}

check_format ts "video audio"
check_format mkv "video audio"
check_format mka "audio"
check_format mp4 "video audio"
check_format m4a "audio"

echo "OK"

#EOF
