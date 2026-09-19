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

TLSPORT=19226
HTTPPORT=19227

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

# QUIC v1 probes. server decides on Retry, version neg and token handling from header alone, payload can be random
cat >"$WORK/probe.py" <<'EOF'
import os, socket, sys

port = int(sys.argv[1])
what = sys.argv[2]

def pkt_initial(dcid, scid, token, version=1):
    hdr = bytes([0xC3]) + version.to_bytes(4, "big")
    hdr += bytes([len(dcid)]) + dcid + bytes([len(scid)]) + scid
    hdr += bytes([len(token)]) + token
    rest = 1200 - len(hdr) - 2
    hdr += (0x4000 | rest).to_bytes(2, "big")
    return hdr + os.urandom(rest)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.0)
dcid, scid = os.urandom(8), os.urandom(8)

if what == "initial":
    data = pkt_initial(dcid, scid, b"")
elif what == "badtoken":
    data = pkt_initial(dcid, scid, b"\xb7" + os.urandom(31))
elif what == "version":
    data = pkt_initial(dcid, scid, b"", version=0x0a0a0a0a)
elif what == "short":
    data = bytes([0x40]) + os.urandom(1199)
else:
    sys.exit("bad probe")

sock.sendto(data, ("127.0.0.1", port))
try:
    resp = sock.recv(2048)
except socket.timeout:
    print("none")
    sys.exit(0)

if len(resp) >= 7 and resp[0] & 0x80:
    ver = int.from_bytes(resp[1:5], "big")
    dlen = resp[5]
    rdcid = resp[6:6 + dlen]
    echoed = rdcid == scid
    if ver == 0:
        print("version-negotiation" if echoed else "bad-vn")
    elif resp[0] & 0x30 == 0x30:
        print("retry" if echoed and len(resp) < 1200 else "bad-retry")
    elif resp[0] & 0x30 == 0x00:
        print("initial-close" if echoed else "bad-close")
    else:
        print("other")
else:
    print("reset" if len(resp) < len(data) else "bad-reset")
EOF

DPID=""
start() {
    mode=$1
    timeout 30 "$BIN" -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" --h3-retry "$mode" \
        --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" >"$WORK/dipixy_$mode.log" 2>&1 &
    DPID=$!
    sleep 0.7
    grep -q "http3: quic context ready" "$WORK/dipixy_$mode.log" || {
        stop
        skip "dipixy was built without HTTP/3"
    }
    return 0
}

stop() {
    [ -n "$DPID" ] || return
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
    DPID=""
}

probe() {
    what=$1
    python3 "$WORK/probe.py" "$TLSPORT" "$what" 2>"$WORK/probe.err" || fail "probe $what failed: $(cat "$WORK/probe.err")"
    return 0
}

expect() {
    mode=$1
    what=$2
    want=$3
    got=$(probe "$what")
    [ "$got" = "$want" ] || fail "retry=$mode probe '$what': expected '$want', got '$got'"
    return 0
}

fetch() {
    mode=$1
    curl -sk --http3-only --max-time 10 -o /dev/null -w "%{http_version} %{http_code}\n" \
        "https://127.0.0.1:$TLSPORT/nonexistent" >"$WORK/curl.out" 2>"$WORK/curl.err" \
        || fail "retry=$mode: curl failed, see $WORK/curl.err and $WORK/dipixy_$mode.log"
    [ "$(cat "$WORK/curl.out")" = "3 404" ] || fail "retry=$mode: unexpected curl result: $(cat "$WORK/curl.out")"
    return 0
}

start always
expect always initial retry
expect always badtoken initial-close
expect always version version-negotiation
expect always short reset
fetch always
stop

start off
expect off initial none
expect off version version-negotiation
expect off short reset
fetch off
stop

start auto
expect auto initial none
fetch auto
stop

echo "OK"
