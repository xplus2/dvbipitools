#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

FPB=$(free_port_block 4)
run_tvhead_link_validation "Rist" "rist://127.0.0.1:$FPB" "rist://@127.0.0.1:$FPB" "$(unique_mcast 42)" $((FPB + 2)) "no librist support"

#EOF
