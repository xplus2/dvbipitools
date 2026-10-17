#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

mkdir -p "$WORK/applets/dipiscan"
ln -s "$BIN" "$WORK/applets/dipiscan/dipiscan" || skip "cannot create symlink"
APPLET="$WORK/applets/dipiscan/dipiscan"
: >"$WORK/empty.yaml"
ONE=239.250.250.1-239.250.250.1

scan_rc() {
    want=$1
    shift
    "$APPLET" "$@" >"$WORK/out.log" 2>&1
    got=$?
    [ "$got" = "$want" ] || fail "dipiscan $*: expected exit $want, got $got"
}

scan_rc 0 -h
scan_rc 2 -m 239.250.250.0/8
scan_rc 2 -m bogus
scan_rc 2 -m $ONE -p 0
scan_rc 2 -m $ONE -f bad
scan_rc 2 -m $ONE -t 0
scan_rc 2 -m $ONE -j 0
scan_rc 2 -m $ONE -u bad:
scan_rc 2 -m $ONE -x %z
scan_rc 2 -m $ONE --color bad
scan_rc 2 -m $ONE extra
scan_rc 2 -m $ONE -f xml
scan_rc 2 --bogus
scan_rc 2 -c "$WORK/missing.yaml" -m $ONE
scan_rc 2 --configtest -c "$WORK/missing.yaml"

scan_rc 0 -m $ONE -p 5000 -t 1 -j 1 -f null -v --color never -I lo -M -u 127.0.0.1:1 -x /%g/%p -P prov -o "$WORK/scan.out" -c "$WORK/empty.yaml"
scan_rc 0 -m $ONE -p 5000-5001 -t 1 -f xml -P prov -x /%g

echo "OK"

#EOF
