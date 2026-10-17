#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

run_as_applet "$(dirname "$0")/../dipibim/xml_to_bim_to_xml_round_trip.sh" dipibim

#EOF
