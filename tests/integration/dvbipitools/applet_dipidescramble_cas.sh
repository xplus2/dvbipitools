#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

run_as_applet "$(dirname "$0")/../dipidescramble/cas_plumbing_validation.sh" dipidescramble dipitvhead

#EOF
