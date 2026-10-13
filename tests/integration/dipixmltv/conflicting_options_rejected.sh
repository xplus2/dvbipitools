#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

printf '<tv></tv>\n' > "$WORK/in.xmltv"
: > "$WORK/map.csv"

run_expect_rc 2 "-R with xmltv" "$BIN" -f xmltv -M "$WORK/map.csv" -R "$WORK/map.csv" -i "$WORK/in.xmltv" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "-R not allowed with -f xmltv" "-R with xmltv"

run_expect_rc 2 "-M with tva" "$BIN" -f tva -M "$WORK/map.csv" -i "$WORK/in.xmltv" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "-M not allowed with -f tva" "-M with tva"

run_expect_rc 2 "-S with -f" "$BIN" -S "$WORK/map.csv" -f tva -i "$WORK/in.xmltv" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "-f not allowed with -S" "-S with -f"

run_expect_rc 2 "-S with -M" "$BIN" -S "$WORK/map.csv" -M "$WORK/map.csv" -i "$WORK/in.xmltv" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "-M not allowed with -S" "-S with -M"

run_expect_rc 2 "-S with -R" "$BIN" -S "$WORK/map.csv" -R "$WORK/map.csv" -i "$WORK/in.xmltv" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "-R not allowed with -S" "-S with -R"

echo "OK"

#EOF
