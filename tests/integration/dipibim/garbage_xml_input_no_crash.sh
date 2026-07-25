#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

# the TVA-XML reader is a lenient pattern scanner, not a validating parser:
# garbage input must not crash, and yields an empty (not error) document
printf 'this is not xml at all, just %s garbage\n' "$(head -c 200 /dev/urandom | tr -dc 'a-zA-Z0-9')" > "$WORK/garbage.xml"

run_expect_rc 0 "encode" "$BIN" -f xml -i "$WORK/garbage.xml" -o "$WORK/out.bim"

echo "OK"

#EOF