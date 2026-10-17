#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

mkdir -p "$WORK/applets/dipisds"
ln -s "$BIN" "$WORK/applets/dipisds/dipisds" || skip "cannot create symlink"
APPLET="$WORK/applets/dipisds/dipisds"

MCAST=$(unique_mcast 92)
PORT=$(free_udp_port)

cat >"$WORK/channels.csv" <<EOT
svc1,udp://239.1.9.1:5000
svc2,udp://239.1.9.2:5000
EOT

timeout 10 "$APPLET" -l -m $MCAST:$PORT -I lo -t 6 -o "$WORK/listen.out" -v >"$WORK/listen.log" 2>&1 &
LPID=$!
sleep 0.5
timeout 4 "$APPLET" -a -i "$WORK/channels.csv" -p example.org -O "Test Headend" -m $MCAST:$PORT -I lo -t 1 >"$WORK/announce.log" 2>&1
wait $LPID
[ -s "$WORK/listen.out" ] || fail "dipisds listen: no output (see $WORK/listen.log)"
grep -q "svc1" "$WORK/listen.out" || fail "dipisds listen: announced service missing"

echo "OK"

#EOF
