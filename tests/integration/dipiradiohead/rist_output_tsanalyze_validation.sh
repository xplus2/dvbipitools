#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
RX_BIN=$2
. "$(dirname "$0")/../common.sh"

run_radiohead_link_validation "Rist" "rist://127.0.0.1:41050" "rist://@127.0.0.1:41050" 239.255.41.50 41051 41052 "no librist support" "$RX_BIN"

#EOF
