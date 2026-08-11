#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_expect_rc 2 "no args at all" "$BIN"
run_expect_rc 2 "missing -g" "$BIN" -l 127.0.0.1:6000 -I lo
run_expect_rc 2 "missing -l" "$BIN" -g 239.0.0.0/8 -I lo
run_expect_rc 2 "missing -I" "$BIN" -g 239.0.0.0/8 -l 127.0.0.1:6000
run_expect_rc 2 "--no-ret and --no-fcc together" "$BIN" -g 239.0.0.0/8 -l 127.0.0.1:6000 -I lo --no-ret --no-fcc

echo "OK"

#EOF
