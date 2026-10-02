# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# sourced by integration test scripts, not run directly.
# caller sets BIN from $1 before sourcing

if [ -z "${BIN:-}" ] || [ ! -x "$BIN" ]; then
    echo "FAIL: no executable binary given as \$1 ($BIN)" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# exit code recognized by ctest SKIP_RETURN_CODE (tests/CMakeLists.txt)
skip() {
    echo "SKIP: $*" >&2
    exit 77
}

# run_expect_rc <expected-rc> <label> -- rest of the line runs
run_expect_rc() {
    want=$1
    label=$2
    shift 2
    "$@"
    got=$?
    if [ "$got" != "$want" ]; then
        fail "$label: expected exit $want, got $got"
    fi
}

assert_contains() {
    file=$1
    pattern=$2
    label=$3
    if ! grep -q -- "$pattern" "$file"; then
        fail "$label: expected to find '$pattern' in $file"
    fi
}

assert_not_contains() {
    file=$1
    pattern=$2
    label=$3
    if grep -q -- "$pattern" "$file"; then
        fail "$label: did not expect to find '$pattern' in $file"
    fi
}

gen_test_clip() {
    ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=320x240:rate=25" \
        -f lavfi -i "sine=frequency=$2" -t "$3" \
        -c:v libx264 -preset ultrafast -c:a aac -f mpegts "$1"
}

run_tvhead_link_validation() {
    label=$1
    out_uri=$2
    in_uri=$3
    mcast=$4
    port=$5
    skip_pattern=$6

    for t in ffmpeg tsp tsanalyze jq; do
        command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
    done

    clip="$WORK/clip.ts"
    cap="$WORK/capture.ts"
    report="$WORK/report.json"

    gen_test_clip "$clip" 1000 3

    tsp -I ip "$mcast:$port" --local-address 127.0.0.1 --receive-timeout 3000 -O file "$cap" >"$WORK/tsp.log" 2>&1 &
    tsp_pid=$!

    "$BIN" -i "$in_uri" -O lo -u -m "$mcast:$port" >"$WORK/receiver.log" 2>&1 &
    rx_pid=$!
    sleep 0.5
    if ! kill -0 "$rx_pid" 2>/dev/null; then
        grep -q "$skip_pattern" "$WORK/receiver.log" && skip "$label transport not built in"
        fail "$label: receiver exited early (see $WORK/receiver.log)"
    fi

    "$BIN" -i - -R "$out_uri" -s "$label Channel" <"$clip" >"$WORK/sender.log" 2>&1
    sleep 2
    kill -INT "$rx_pid" 2>/dev/null
    wait "$rx_pid" 2>/dev/null
    wait "$tsp_pid" || true

    [ -s "$cap" ] || fail "$label: no packets captured (see $WORK/sender.log, $WORK/receiver.log)"

    tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "$label: tsanalyze failed, see $WORK/tsanalyze.log"

    cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
    [ "${cc_errors:-0}" = "0" ] || fail "$label: $cc_errors continuity-counter errors in capture"

    services=$(jq '.services | length' "$report")
    [ "${services:-0}" -ge 1 ] || fail "$label: no services found in captured output"

    service_name=$(jq -r '.services[0].name // empty' "$report")
    [ "$service_name" = "$label Channel" ] || fail "$label: expected service name '$label Channel', got '$service_name'"

    echo "OK"
}

run_radiohead_link_validation() {
    label=$1
    out_uri=$2
    in_uri=$3
    mcast=$4
    port=$5
    http_port=$6
    skip_pattern=$7
    rx_bin=$8

    for t in ffmpeg tsp tsanalyze jq python3; do
        command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
    done
    [ -x "$rx_bin" ] || fail "$label: no receiver binary given as \$2 ($rx_bin)"

    cap="$WORK/capture.ts"
    report="$WORK/report.json"

    mkdir -p "$WORK/httproot"
    ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=3" \
        -c:a libmp3lame -f mp3 "$WORK/httproot/stream.mp3"

    (cd "$WORK/httproot" && python3 -u -m http.server "$http_port" --bind 127.0.0.1 \
        >"$WORK/httpd.log" 2>&1) &
    httpd_pid=$!
    trap 'kill $httpd_pid 2>/dev/null; rm -rf "$WORK"' EXIT
    i=0
    while ! grep -q "Serving HTTP" "$WORK/httpd.log" 2>/dev/null; do
        i=$((i + 1))
        [ "$i" -lt 200 ] || fail "$label: http server on $http_port never became ready"
        sleep 0.05
    done

    tsp -I ip "$mcast:$port" --local-address 127.0.0.1 --receive-timeout 2000 -O file "$cap" >"$WORK/tsp.log" 2>&1 &
    tsp_pid=$!

    "$rx_bin" -i "$in_uri" -O lo -u -m "$mcast:$port" >"$WORK/receiver.log" 2>&1 &
    rx_pid=$!
    sleep 0.5
    if ! kill -0 "$rx_pid" 2>/dev/null; then
        grep -q "$skip_pattern" "$WORK/receiver.log" && skip "$label transport not built in"
        fail "$label: receiver exited early (see $WORK/receiver.log)"
    fi

    timeout 15 "$BIN" -i "http://127.0.0.1:$http_port/stream.mp3" -R "$out_uri" -s "$label Station" >"$WORK/sender.log" 2>&1
    if grep -q "$skip_pattern" "$WORK/sender.log"; then
        kill "$rx_pid" 2>/dev/null
        skip "$label output transport not built into the sender"
    fi
    sleep 1
    kill -INT "$rx_pid" 2>/dev/null
    wait "$rx_pid" 2>/dev/null
    wait "$tsp_pid" || true
    kill "$httpd_pid" 2>/dev/null

    [ -s "$cap" ] || fail "$label: no packets captured (see $WORK/sender.log, $WORK/receiver.log)"

    tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "$label: tsanalyze failed, see $WORK/tsanalyze.log"

    cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
    [ "${cc_errors:-0}" = "0" ] || fail "$label: $cc_errors continuity-counter errors in capture"

    service_name=$(jq -r '.services[0].name // empty' "$report")
    [ "$service_name" = "$label Station" ] || fail "$label: expected service name '$label Station', got '$service_name'"

    audio_desc=$(jq -r '.pids[] | select(.audio==true) | .description // empty' "$report" | head -1)
    [ -n "$audio_desc" ] || fail "$label: no audio component in the captured service"

    audio_pes=$(jq -r '[.pids[] | select(.audio==true) | .pes // 0] | add // 0' "$report")
    [ "${audio_pes:-0}" -ge 20 ] || fail "$label: only $audio_pes audio PES packets arrived, expected a 3s station"

    echo "OK"
}

#EOF
