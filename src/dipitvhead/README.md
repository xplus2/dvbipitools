# dipitvhead

IPI TV Headend

Takes a transport stream (multicast, http(s), or stdin ("-") and re-packages it as a DVB-IPI multicast under our own PAT/PMT/SDT.
No transcoding.

```
dipitvhead -i <uri> -m <mcast>:<port> [options]
```

## Options

| flag | long form | argument | default |
|---|---|---|---|
| `-i` | `--input` | `<uri>` / `-` | required |
| `-p` | `--pmt-pid` | `<pid>` | auto: first PAT program whose PMT actually arrives |
| `-m` | `--mcast` | `<group>:<port>` / `[<group6>]:<port>` | required |
| `-I` | `--iface` | `<iface>` | kernel route (incoming) |
| `-O` | `--out-iface` | `<iface>` | kernel route (outgoing) |
| `-u` | `--udp` | - | off (RTP) |
| `-T` | `--ttl` | `<n>` | 1 |
| `-n` | `--nit` | `<text>` / `-` | passthrough source NIT if present |
| `-s` | `--sdt` | `<text>` / `-` | passthrough source SDT if present |
| `-b` | `--bitrate` | `<kbps>` | none (no shaping) |
| `-S` | `--stuff` | - | off (needs `-b`) |
| `-B` | `--burst-limit` | - | off (needs `-b`) |
| | `--strip-eit` | - | off (source EIT passed through) |
| | `--hbbtv` | `<url>` | none (no AIT sent) |
| | `--hbbtv-org-id` | `<n>` | required with `--hbbtv` |
| | `--hbbtv-app-id` | `<n>` | required with `--hbbtv` |
| `-e` | `--error` | `<seconds>` | fail once, no retry |
| `-k` | `--insecure` | - | off (TLS verified) |
| | `--tsid` | `<n>` | 1 |
| | `--onid` | `<n>` | 1 |
| | `--sid` | `<n>` | 1 |
| `-v` | `--verbose` | - | off |
| | `--color` | `auto\|always\|never` | `auto` |
| | `--cas-algo` | `cissa\|csa2` | disabled |
| | `--cas-ecmg` | `tcp://host:port` | required with `--cas-algo` |
| | `--cas-ecmg-version` | `2\|3` | auto-negotiate |
| | `--cas-super-id` | `<n>` | required with `--cas-algo` |
| | `--cas-ecm-id` | `<n>` | required with `--cas-algo` |
| | `--cas-ecm-pid` | `<pid>` | `0x0020` |
| | `--cas-emmg-port` | `<n>` | `8002` |
| | `--cas-emmg-version` | `2\|3` | accept client's proposal |
| | `--cas-emm-pid` | `<pid>` | `0x0021` |
| | `--cas-pids` | `<list>` | required with `--cas-algo` |
| | `--cas-cp-duration` | `<ms>` | `10000` |
| | `--cas-resilience` | `frozen\|cycling\|unscrambled` | `frozen` |
| `-h` | `--help` | - | |

> Note that the default output changed from _plain UDP_ to _RTP_, since neither FCC nor RET would work
> on plain streams. You can restore the old behavior by setting `-u`|`--udp`.

## Parameters

### Input (`-i`)

`udp://`, `rtp://`, `http://`, `https://`, `-` for stdin. RTP headers stripped automatically.
HTTPS: build-time option (`-DDIPITVHEAD_TLS=OFF`, auto-off without OpenSSL), `-k` skips cert
verification.

### Program selection (`-p`)

PAT watched on startup and logged. No `-p`: first PAT-listed program whose PMT actually arrives
wins (real MPTS sources often list many services, stream one). `-p <pid>` forces a PMT PID.

### Codec support

Video: MPEG-2, H.264, HEVC. Audio: MPEG-1/2 (layer 1/2/3), AC-3, E-AC-3, AAC (ADTS/LATM).
Subtitles: EBU teletext, DVB bitmap. Everything else (carousels, SCTE-35, CA/ECM) dropped.
Output PIDs: PAT `0x0000`, PMT `0x1000`, video `0x0100`, other ES `0x0101..` in discovery order,
NIT `0x0010`, SDT `0x0011`, EIT `0x0012`, AIT `0x0020`.

### Service info (`-n`, `-s`)

Default: passthrough the source's own NIT/SDT text if present. `<text>`: our own (provider name
fixed to `dipitvhead`). `-`: drop the table outright.

### EIT (`--strip-eit`)

No EIT reconstruction - source EIT forwarded verbatim (PID remapped, own CC) unless stripped.

### Target bitrate (`-b`, `-S`, `-B`)

No `-b`: source rate passes straight through. `-S`: null-packet padding when output falls behind
target. `-B`: paces sending so output never runs ahead of target. Combinable.

### HbbTV signalling (`--hbbtv`)

Injects an AIT (table_id 0x74), one AUTOSTART application: `--hbbtv-org-id`/`--hbbtv-app-id`
identify it, `--hbbtv` is its entry-point URL (`transport_protocol_descriptor`, protocol_id
0x0003). Not a passthrough of any source AIT - that references carousel PIDs we don't carry.

### Identifiers (`--tsid`, `--onid`, `--sid`)

transport_stream_id / original_network_id / service_id, default 1. `--sid` doubles as the PMT
program_number.

### Reconnecting (`-e`)

No `-e`: any input error stops the tool. `-e <seconds>`: reopens after the delay, output socket
and continuity counters stay up across the gap.

### Live stats (`-v`)

One self-updating line on stderr, about once a second.

## CAS SCS and Scrambler (`--cas-*`)

Acts as a DVB Simulcrypt SCS towards an ECMG, and as the EMMG-side MUX towards an EMMG client,
per ETSI TS 103 197 (protocol versions 2 and 3, auto-negotiated unless `--cas-ecmg-version` /
`--cas-emmg-version` pins one). Scrambles CISSA (ETSI TS 103 127, 128-bit AES-CBC, needs
OpenSSL) or CSA2 (needs libdvbcsa) content on the PIDs listed in `--cas-pids` and emits the
matching `CA_descriptor`/`scrambling_descriptor` in the PMT and a CAT, and its own ECM
(`--cas-ecm-pid`) and EMM (`--cas-emm-pid`) streams.

### Requires a real PCR

Crypto-period cadence is driven by the source's own PCR, not wall-clock or a configured bitrate (VBR support).
DVB time is king. `dipitvhead` fails fast if no PCR is observed on the PCR_PID within a few seconds of startup. 
PCR discontinuities (splice, failover) are handled gracefully at runtime, using wall-clock only 
as a plausibility fence to reject bogus jumps.

### On ECMG loss (`--cas-resilience`)

* `frozen` (default): keeps scrambling with the last known-good CW indefinitely until the ECMG
comes back. The crypto period stays put on whatever it was, the service never blacks out.
* `cycling`: keeps flipping parity on the normal crypto-period schedule, alternating between
the last two known CWs (even/odd), instead of freezing on one.
* `unscrambled`: the CW expires one crypto period after the ECMG becomes unreachable. Affected
PIDs fall back to clear (`transport_scrambling_control` = 00) until a fresh CW arrives.

### EMMG (`--cas-emmg-port`, `--cas-emmg-version`)

dipitvhead is the EMMG-side MUX: it listens (`--cas-emmg-port`, default 8002) and the EMMG
client connects to it. For now, the only topology, not the reversed one where the MUX
dials out to the EMMG. Accepts whichever protocol version the client proposes unless
`--cas-emmg-version` is set. EMM datagrams are queued and drained onto `--cas-emm-pid` on
arrival.

### Limitations

CISSA and CSA2 only - no CSA3. One ECMG connection, one EMMG listener, one Super_CAS_id.

### Dependencies

CAS support is a CMake/configure-time option (`DIPITVHEAD_CAS`, on by default). 
* CISSA needs OpenSSL (same dependency as HTTPS input)
* CSA2 needs libdvbcsa (`DIPITVHEAD_CSA2` / `HAVE_DVBCSA`). 

Missing either degrades gracefully to "that algorithm unavailable", not a build failure.

Release builds and the packaged `.deb` in this repository do not contain libdvbcsa. 
Build against it yourself if you want CSA2.


## Stopping

`^C`, SIGINT or SIGTERM.

## Examples

```sh
dipitvhead -i rtp://@239.2.24.1:8208 -m 239.1.1.1:5000 -s "My Channel"

dipitvhead -i https://host/live/x/y.ts -k -m 239.1.1.2:5000 -b 8000 -S -B

dipitvhead -i udp://@239.0.0.1:5000 -m 239.5.5.5:6000 \
  --hbbtv https://example.org/hbbtv/ --hbbtv-org-id 1 --hbbtv-app-id 100

# enigma2 DVB-S2 MPTS source (would need "-p" in reality)
dipitvhead -i http://receiver:8001/1:0:10:10:3EF:1:C00000:0:0:0: -m 239.5.5.5:6000

# enigma2 + oscam relay: MPTS, live program picked automatically
dipitvhead -i http://receiver:17555/1:0:CA:CA:C:85:C00000:0:0:0: -m 239.5.5.6:6000

# scramble an SPTS and re-stream it
dipitvhead -i rtp://@239.2.24.1:8208 -m 239.1.1.1:5000 \
  --cas-algo cissa --cas-ecmg tcp://ecmg.example:2222 --cas-super-id 0x4A750002 --cas-ecm-id 1 --cas-pids 0x0100,0x0101

# transcoding: not our job, but ffmpeg pipes straight in
ffmpeg -i <source> -c:v libx264 -c:a aac -f mpegts - | dipitvhead -i - -m 239.5.5.5:6000
```
