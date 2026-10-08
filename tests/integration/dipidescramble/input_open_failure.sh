#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

MCAST=$(unique_mcast 62)

run_expect_rc 1 "unknown input interface" \
    sh -c "timeout 8 '$BIN' -i 'udp://@$MCAST:5000' -I nosuchif0 -o '$WORK/out.ts' -f ts >'$WORK/log' 2>&1"
assert_contains "$WORK/log" "cannot open -i" "input open failure reported"
echo "OK"

#EOF
