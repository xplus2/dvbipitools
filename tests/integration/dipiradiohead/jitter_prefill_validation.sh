#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v python3 >/dev/null 2>&1 || fail "required tool 'python3' not found on PATH"

# 1 s burst, then real time, one 2 s stall. longest output gap in first 8 s (EOF flush excluded)
cat >"$WORK/run.py" <<'EOF'
import socket, struct, sys, threading, time

http_port, group, mport, out = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
FRAME = bytes([0xFF, 0xFB, 0x90, 0x00]) + bytes(413)
DT = 1152 / 44100.0
NFRAMES = 330
STALL_AT = 130
STALL_S = 2.0

stamps = []
stop = threading.Event()

def listen():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", mport))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(group) + socket.inet_aton("127.0.0.1"))
    s.settimeout(0.2)
    while not stop.is_set():
        try:
            s.recv(2048)
            stamps.append(time.monotonic())
        except socket.timeout:
            pass

t = threading.Thread(target=listen)
t.start()
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", http_port))
srv.listen(1)
srv.settimeout(10)
c, _ = srv.accept()
c.recv(4096)
c.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\n")
t0 = time.monotonic()
burst = int(1.0 / DT)
extra = 0.0
for i in range(NFRAMES):
    if i == STALL_AT:
        extra = STALL_S
    due = t0 + max(0, i - burst) * DT + extra
    d = due - time.monotonic()
    if d > 0:
        time.sleep(d)
    c.sendall(FRAME)
time.sleep(1.0)
c.close()
stop.set()
t.join()
win = [x for x in stamps if x - stamps[0] < 8.0] if stamps else []
gap = max((b - a for a, b in zip(win, win[1:])), default=999.0)
open(out, "w").write("%d %.3f\n" % (len(win), gap))
EOF

run_phase() {
    name=$1
    http_port=$2
    mport=$3
    shift 3
    python3 "$WORK/run.py" "$http_port" 239.255.7.11 "$mport" "$WORK/$name.res" &
    py_pid=$!
    sleep 0.5
    "$BIN" -i "http://127.0.0.1:$http_port/stream" -O lo -m "239.255.7.11:$mport" "$@" >"$WORK/$name.log" 2>&1 &
    tool_pid=$!
    wait "$py_pid" || fail "$name: helper failed"
    kill -INT "$tool_pid" 2>/dev/null
    wait "$tool_pid" 2>/dev/null
    return $?
}

run_phase control 17811 17812 &
ctl=$!
run_phase jitter 17813 17814 --jitter-ms 3000 &
jit=$!
wait "$ctl"
wait "$jit"

[ -s "$WORK/control.res" ] || fail "control: no result (see $WORK/control.log)"
[ -s "$WORK/jitter.res" ] || fail "jitter: no result (see $WORK/jitter.log)"

read -r ctl_n ctl_gap <"$WORK/control.res"
read -r jit_n jit_gap <"$WORK/jitter.res"

[ "$ctl_n" -gt 100 ] || fail "control: only $ctl_n datagrams"
[ "$jit_n" -gt 100 ] || fail "jitter: only $jit_n datagrams"

python3 -c "import sys; sys.exit(0 if float('$ctl_gap') > 1.0 else 1)" || fail "control: expected stall gap > 1.0 s, got $ctl_gap s"
python3 -c "import sys; sys.exit(0 if float('$jit_gap') < 0.5 else 1)" || fail "jitter: stall not bridged, longest gap $jit_gap s"

command -v ffmpeg >/dev/null 2>&1 || fail "'ffmpeg' not in PATH"

ffmpeg -loglevel error -f lavfi -i "sine=frequency=440:duration=12" -c:a aac -b:a 64k -f segment -segment_time 2 -segment_format mpegts "$WORK/seg%d.ts" || fail "ffmpeg failed"

cat >"$WORK/hls.py" <<'EOF'
import http.server, os, sys

work, port = sys.argv[1], int(sys.argv[2])

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write((fmt % args) + "\n")

    def do_GET(self):
        if self.path.endswith(".m3u8"):
            body = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
            for i in range(5):
                body += "#EXTINF:2.0,\nseg%d.ts\n" % i
            data = body.encode()
            ctype = "application/vnd.apple.mpegurl"
        else:
            data = open(os.path.join(work, os.path.basename(self.path)), "rb").read()
            ctype = "video/mp2t"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
EOF

python3 "$WORK/hls.py" "$WORK" 17815 2>"$WORK/hls_server.log" &
hls_pid=$!
sleep 0.5
"$BIN" -i "http://127.0.0.1:17815/live.m3u8" -m 239.255.7.11:17816 --jitter-ms 1000 >"$WORK/hls.log" 2>&1 &
tool_pid=$!
sleep 5
kill -INT "$tool_pid" 2>/dev/null
wait "$tool_pid" 2>/dev/null
kill "$hls_pid" 2>/dev/null

assert_contains "$WORK/hls_server.log" "GET /seg" "HLS input with --jitter-ms fetches segments"

echo "OK"
