#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg ffprobe; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

clip="$WORK/clip.ts"
ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=320x240:rate=25" -f lavfi -i "sine=frequency=1000" -t 3 \
    -c:v libx264 -preset ultrafast -g 10 -c:a aac -f mpegts "$clip" || fail "cannot build the test clip"

types() {
    ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$1" | sort -u | tr '\n' ' '
}

"$BIN" -i - -f mkv -o "$WORK/out.mkv" <"$clip" >"$WORK/mkv.log" 2>&1 || fail "-f mkv failed (see $WORK/mkv.log)"
got=$(types "$WORK/out.mkv")
[ "$got" = "audio video " ] || fail "mkv: expected audio and video streams, got '$got'"

"$BIN" -i - -f mka -o "$WORK/out.mka" <"$clip" >"$WORK/mka.log" 2>&1 || fail "-f mka failed (see $WORK/mka.log)"
got=$(types "$WORK/out.mka")
[ "$got" = "audio " ] || fail "mka: expected audio only, got '$got'"

echo "OK"

#EOF
