#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_tvhead_link_validation "Srt" "srt://127.0.0.1:41020" "srt://@127.0.0.1:41020" 239.255.41.20 41021 "no libsrt support"

#EOF
