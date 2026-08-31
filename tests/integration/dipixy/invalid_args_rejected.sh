#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_expect_rc 2 "segment-size below min" "$BIN" --segment-size 1
run_expect_rc 2 "segment-count below min" "$BIN" --segment-count 2
run_expect_rc 2 "hls-part-size >= segment-size" "$BIN" --segment-size 3 --hls-part-size 5
run_expect_rc 2 "malformed -l address" "$BIN" -l "not-an-address"

echo "OK"

#EOF
