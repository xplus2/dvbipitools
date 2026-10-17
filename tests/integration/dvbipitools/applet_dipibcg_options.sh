#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

mkdir -p "$WORK/applets/dipibcg"
ln -s "$BIN" "$WORK/applets/dipibcg/dipibcg" || skip "cannot create symlink"
APPLET="$WORK/applets/dipibcg/dipibcg"
: >"$WORK/empty.yaml"
G=239.1.1.1:5000

bcg_rc() {
    want=$1
    shift
    "$APPLET" "$@" >"$WORK/out.log" 2>&1
    got=$?
    [ "$got" = "$want" ] || fail "dipibcg $*: expected exit $want, got $got"
}

bcg_rc 0 -h
bcg_rc 2 --configtest -c "$WORK/empty.yaml" --config-strict
bcg_rc 2 -c "$WORK/missing.yaml" -a -m $G
bcg_rc 2 -l -m $G extra
bcg_rc 2 -a -m $G -i x -M y -w bad
bcg_rc 2 -a -m bad -i x -M y
bcg_rc 2 -a -m $G -i x -M y --refresh 0
bcg_rc 2 -a -m $G -i x -M y --color bad
bcg_rc 2 -a -m $G -i x -M y --dscp bad
bcg_rc 2 -a -m $G -i x -M y -t 0
bcg_rc 2 -a -m $G -i x -M y --timeout 1
bcg_rc 2 -a -m $G -i x -M y --bogus
bcg_rc 2 -l -m $G -Z
bcg_rc 2 -l -m $G --refresh 2
bcg_rc 2 -l -m $G --metrics-id x
bcg_rc 2 -l -m $G --interval 3
bcg_rc 2 -a -m $G -M y
bcg_rc 2 -a -m $G -i x
bcg_rc 2 -a -i x -M y
bcg_rc 2 -m $G
bcg_rc 2 -l -a -m $G
bcg_rc 2 -a -m $G -i x -M y --metrics "$WORK/m.sock"

bcg_rc 1 -a -m $G -i "$WORK/missing.xml" -M "$WORK/missing.csv" -w 2 --interval 5 -o "$WORK/o" -C "$WORK/c.csv" --refresh 2 -Z -v --color never --dscp voice -I lo --metrics "$WORK/m.sock" --metrics-id x --metrics-interval 5 -c "$WORK/empty.yaml"
grep -q "cannot open" "$WORK/out.log" || fail "dipibcg announce: missing input not reported"

bcg_rc 1 -l -m $G --timeout 1 -o "$WORK/listen.out" -v --color never --dscp voice -I lo -c "$WORK/empty.yaml"
grep -q "listening on" "$WORK/out.log" || fail "dipibcg listen: no listen banner"

echo "OK"

#EOF
