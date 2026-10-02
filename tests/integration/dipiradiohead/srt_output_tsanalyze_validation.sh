#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
RX_BIN=$2
. "$(dirname "$0")/../common.sh"

run_radiohead_link_validation "Srt" "srt://127.0.0.1:41060" "srt://@127.0.0.1:41060" 239.255.41.60 41061 41062 "no libsrt support" "$RX_BIN"

#EOF
