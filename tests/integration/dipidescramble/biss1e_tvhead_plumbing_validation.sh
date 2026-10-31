#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v ffmpeg >/dev/null 2>&1 || fail "required tool 'ffmpeg' not found on PATH"
command -v ffprobe >/dev/null 2>&1 || fail "required tool 'ffprobe' not found on PATH"

DIPITVHEAD=$(echo "$BIN" | sed 's#/dipidescramble\([^/]*\)$#/../dipitvhead/dipitvhead\1#')
[ -x "$DIPITVHEAD" ] || DIPITVHEAD="./dipitvhead"
[ -x "$DIPITVHEAD" ] || fail "cannot locate dipitvhead binary (tried $DIPITVHEAD)"

SW=0123456789ab
ID=fb5f9c585dd359
WRONG_ID=dc91106f13c87b

# name, descrambler id, then extra flags as "emit" "receive"
run_case() {
    name=$1
    rx_id=$2
    emit_flag=$3
    rx_flag=$4
    mcast=$(unique_mcast 61)
    port=$(free_port_block 1)
    out="$WORK/$name.ts"
    tvlog="$WORK/$name-dipitvhead.log"
    dslog="$WORK/$name-dipidescramble.log"

    ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
        -f lavfi -i "sine=frequency=1000" -t 7 \
        -c:v libx264 -preset ultrafast -g 25 -c:a aac -f mpegts - 2>"$WORK/$name-ffmpeg.log" | \
    timeout 10 "$DIPITVHEAD" -O lo -u -m "$mcast:$port" -i - -s "BISS1E Test" \
        --biss1-sw "$SW" --biss1-emit-esw "$ID" $emit_flag --cas-pids video,audio \
        >"$tvlog" 2>&1 &
    tvpid=$!

    wait_until 5 log_has "$tvlog" "--biss1-emit-esw ID: " || fail "$name: no ESW logged"
    esw=$(sed -n 's/.*--biss1-emit-esw ID: \([0-9a-fA-F]*\).*/\1/p' "$tvlog" | head -n 1)
    [ "${#esw}" -eq 16 ] || fail "$name: bad ESW '$esw'"

    "$BIN" -i "udp://@$mcast:$port" -I lo --biss1-esw "$esw" --biss1-id "$rx_id" $rx_flag \
        -o "$out" -f ts >"$dslog" 2>&1 &
    dspid=$!

    wait $tvpid 2>/dev/null
    sleep 1
    kill $dspid 2>/dev/null
    wait $dspid 2>/dev/null

    assert_contains "$dslog" "BISS Mode 1/E detected (ca_system_id=0x2600)" "$name: BISS1 detection"
    [ -s "$out" ] || fail "$name: no output file produced"
}

decoded_frames() {
    ffprobe -v quiet -select_streams v -count_frames -show_entries stream=nb_read_frames \
        -of csv=p=0 "$1" 2>/dev/null | head -n 1 | tr -cd '0-9'
}

run_case injected "$ID" "" ""
frames=$(decoded_frames "$WORK/injected.ts")
[ "${frames:-0}" -gt 0 ] || fail "injected: output does not decode"

run_case buried "$ID" --biss1-emit-esw-buried --biss1-id-buried
frames=$(decoded_frames "$WORK/buried.ts")
[ "${frames:-0}" -gt 0 ] || fail "buried: output does not decode"

run_case wrongid "$WRONG_ID" "" ""
frames=$(decoded_frames "$WORK/wrongid.ts")
[ "${frames:-0}" -eq 0 ] || fail "wrongid: output decoded under wrong ID"

echo "OK"

#EOF
