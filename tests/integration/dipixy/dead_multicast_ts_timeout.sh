#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v curl >/dev/null 2>&1 || fail "required tool 'curl' not found on PATH"

HTTPPORT=19208
MCAST=239.255.9.30
MPORT=18200

# nothing ever sends to MCAST:MPORT: the join succeeds, no packet ever arrives.
timeout 15 "$BIN" -l "127.0.0.1:$HTTPPORT" --ts-startup-timeout 1 >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.5

code=$(timeout 8 curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT/ts")
rc=$?

kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

[ "$rc" = 124 ] && fail "request hung past curl's 8s timeout, see $WORK/dipixy.log"
[ "$code" = "504" ] || fail "expected 504 Gateway Timeout, got '$code', see $WORK/dipixy.log"

echo "OK"

#EOF
