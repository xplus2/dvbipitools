#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
RX_BIN=$2
. "$(dirname "$0")/../common.sh"

FPB=$(free_port_block 3)
run_radiohead_link_validation "Srt" "srt://127.0.0.1:$FPB" "srt://@127.0.0.1:$FPB" "$(unique_mcast 44)" $((FPB + 1)) $((FPB + 2)) "no libsrt support" "$RX_BIN"

#EOF
