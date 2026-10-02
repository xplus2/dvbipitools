#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_tvhead_link_validation "Rist" "rist://127.0.0.1:41010" "rist://@127.0.0.1:41010" 239.255.41.10 41011 "no librist support"

#EOF
