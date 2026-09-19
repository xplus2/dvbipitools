#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in curl openssl python3; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

curl -V | grep -q "HTTP3" || skip "curl was not built with HTTP/3 support"

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

TLSPORT=19236
HTTPPORT=19237
RELAYPORT=19238
REQUESTS=60

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

# UDP relay client<->server, switches its server-side source port after SWITCH_AFTER s
cat >"$WORK/relay.py" <<'EOF'
import select, socket, sys, time

relay_port, server_port, stop_file, out_file = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
SWITCH_AFTER = 0.8

def udp(port=0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", port))
    return s

front = udp(relay_port)
back = [udp(), udp()]
active = 0
client = None
first = None
stats = {"switched": 0, "replies_old": 0, "replies_new": 0}
server = ("127.0.0.1", server_port)
deadline = time.time() + 25

import os
while time.time() < deadline and not os.path.exists(stop_file):
    r, _, _ = select.select([front, back[0], back[1]], [], [], 0.1)
    for s in r:
        data, addr = s.recvfrom(4096)
        if s is front:
            client = addr
            if first is None:
                first = time.time()
            if not stats["switched"] and time.time() - first >= SWITCH_AFTER:
                active = 1
                stats["switched"] = 1
            back[active].sendto(data, server)
        elif client:
            stats["replies_new" if s is back[1] else "replies_old"] += 1
            front.sendto(data, client)

with open(out_file, "w") as f:
    f.write("%d %d %d\n" % (stats["switched"], stats["replies_old"], stats["replies_new"]))
EOF

timeout 40 "$BIN" -j 8 -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" \
    --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.7

stop_bg() {
    touch "$WORK/stop"
    [ -n "$RPID" ] && wait $RPID 2>/dev/null
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
    return 0
}

grep -q "http3: quic context ready" "$WORK/dipixy.log" || {
    stop_bg
    skip "dipixy was built without HTTP/3"
}

python3 "$WORK/relay.py" "$RELAYPORT" "$TLSPORT" "$WORK/stop" "$WORK/relay.out" >"$WORK/relay.log" 2>&1 &
RPID=$!
sleep 0.3

urls=""
i=0
while [ $i -lt $REQUESTS ]; do
    urls="$urls https://127.0.0.1:$RELAYPORT/nonexistent$i"
    i=$((i + 1))
done

# paced so the requests span the port switch
# shellcheck disable=SC2086
curl -sk --http3-only --rate 40/s --max-time 30 -o /dev/null \
    -w "%{http_version} %{http_code} %{num_connects}\n" $urls >"$WORK/out.txt" 2>"$WORK/curl.err"
rc=$?
stop_bg

[ $rc -eq 0 ] || fail "curl rc=$rc after $(wc -l <"$WORK/out.txt") of $REQUESTS requests, see $WORK/curl.err and $WORK/dipixy.log"

[ "$(wc -l <"$WORK/out.txt")" -eq $REQUESTS ] || fail "not all $REQUESTS requests completed"
[ "$(grep -vc "^3 404 " "$WORK/out.txt")" -eq 0 ] || fail "responses were not all HTTP/3 404"

conns=$(awk '{ if ($3 > 0) n++ } END { print n + 0 }' "$WORK/out.txt")
[ "$conns" -eq 1 ] || fail "expected 1 QUIC connection, curl opened $conns"

read -r switched old new <"$WORK/relay.out" || fail "relay wrote no stats, see $WORK/relay.log"
[ "$switched" -eq 1 ] || fail "relay never switched source port"
[ "$old" -gt 0 ] || fail "no server replies before the switch"
[ "$new" -gt 0 ] || fail "server never replied to the new source port ($old before, $new after)"

echo "OK"
