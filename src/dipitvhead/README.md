# dipitvhead

Takes one or more transport streams (multicast, http(s), or stdin ("-") and re-packages them as
one DVB-IPI multicast under our own PAT/NIT (shared, whole mux) and one PMT/SDT per program (its
own service_name, not shared with the others). 

A single `-i`: normal Single Program Transport Stream (SPTS). 

Multiple `-i`: Multi Program Transport Stream (MPTS), one program per input, each independently connected and retried.
One input being down never stops output for the others. No transcoding.

## General Usage
```
dipitvhead -i <uri> [per-input options] [-i <uri> ...] -m <mcast>:<port> [options]
```

## Options

`-i` is repeatable. Flags marked **per-input** pair with the `-i` immediately before them
(ffmpeg-style) - using one before any `-i` is an error. Everything else is mux-wide, shared
across every input.

| flag | long form            | argument              | default                                   | scope      |
|------|----------------------|-----------------------|-------------------------------------------|------------|
| `-i` | `--input`            | `<uri>` / `-`         | required                                  |            |
| `-p` | `--pmt-pid`          | `<pid>`               | auto: first PAT program whose PMT arrives | per-input  |
|      | `--sid`              | `<n>`                 | auto-assigned (lowest free integer)       | per-input  |
| `-s` | `--sdt`              | `<text>` / `-`        | set SDT, see below                        | per-input  |
| `-I` | `--iface`            | `<iface>`             | kernel route (incoming)                   | per-input  |
|      | `--strip-eit`        |                       | off (source EIT passed through)           | per-input  |
|      | `--hbbtv`            | `<url>`               | none (no AIT sent)                        | per-input  |
|      | `--hbbtv-org-id`     | `<n>`                 | required with `--hbbtv`                   | per-input  |
|      | `--hbbtv-app-id`     | `<n>`                 | required with `--hbbtv`                   | per-input  |
| `-m` | `--mcast`            | `<group(6)>:<port>`   | required                                  |            |
| `-O` | `--out-iface`        | `<iface>`             | kernel route (outgoing)                   |            |
| `-u` | `--udp`              |                       | off (RTP)                                 |            |
| `-T` | `--ttl`              | `<n>`                 | 1                                         |            |
| `-n` | `--nit`              | `<text>` / `-`        | set NIT, see below                        |            |
| `-b` | `--bitrate`          | `<kbps>`              | none (no shaping)                         |            |
| `-S` | `--stuff`            |                       | off (needs `-b`)                          |            |
| `-B` | `--burst-limit`      |                       | off (needs `-b`)                          |            |
| `-e` | `--error`            | `<seconds>`           | fail once (always retries in MPTS mode)   |            |
| `-k` | `--insecure`         |                       | off (TLS verified)                        |            |
|      | `--tsid`             | `<n>`                 | 1                                         |            |
|      | `--onid`             | `<n>`                 | 1                                         |            |
| `-v` | `--verbose`          |                       | off                                       |            |
|      | `--color`            | `auto\|always\|never` | `auto`                                    |            |
| `-h` | `--help`             |                       |                                           |            |

> Note that the default output changed from _plain UDP_ to _RTP_, since neither FCC nor RET would work
> on plain streams. You can restore the old behavior by setting `-u`|`--udp`.

### Conditional Access: SimulCrypt

`--cas-ecmg` is repeatable, one CAS vendor per `--cas-ecmg`. Flags marked **per-vendor** pair
with the `--cas-ecmg` immediately before them, same convention as `-i`'s per-input flags above -
using one before any `--cas-ecmg` is an error. Everything else is shared across every vendor.

| long form              | argument                  | default                                    | scope      |
|------------------------|---------------------------|--------------------------------------------|------------|
| `--cas-algo`           | `cissa\|csa2\|csa1`       | disabled                                   |            |
| `--cas-ecmg`           | `tcp://host:port`         | at least one required with `--cas-algo`    |            |
| `--cas-ecmg-version`   | `2\|3`                    | auto-negotiate                             | per-vendor |
| `--cas-super-id`       | `<n>`                     | required per vendor                        | per-vendor |
| `--cas-ecm-id`         | `<n>`                     | required per vendor                        | per-vendor |
| `--cas-ecm-pid`        | `<pid>`                   | `0x0020`                                   | per-vendor |
| `--cas-emmg-port`      | `<n>`                     | `8002`                                     | per-vendor |
| `--cas-emmg-version`   | `2\|3`                    | accept client's proposal                   | per-vendor |
| `--cas-emm-pid`        | `<pid>`                   | `0x0021`                                   | per-vendor |
| `--cas-resilience`     | `frozen\|cycling\|silent` | `frozen`                                   | per-vendor |
| `--cas-required`       |                           | off                                        | per-vendor |
| `--cas-pids`           | `<list>`                  | `video,audio`                              |            |
| `--cas-cp-duration`    | `<ms>`                    | `10000`                                    |            |
| `--cas-fallback-clear` |                           | off (stay scrambled on last known-good CW) |            |

### Conditional Access: BISS

BISS modes are mutually exclusive with `--cas-algo`/`--cas-ecmg` and with each other.

| long form                | argument   | default                                             |
|--------------------------|------------|------------------------------------------------------|
| `--biss1-sw`             | `<hex12>`  | BISS1 Mode 1                                        |
| `--biss2-sw`             | `<hex32>`  | BISS2 Mode 1/E                                      |
| `--biss2-emit-esw`       | `<hex32>`  | log the Encrypted Session Word for this receiver ID |
| `--biss2-ca-receivers`   | `<dir>`    | BISS2 Mode CA: directory of receiver PEM pubkeys    |
| `--biss2-ca-session-id`  | `<n>`      | dec or 0x-hex, 16 bit; random if not given          |


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

Output PIDs: PAT `0x0000`, NIT `0x0010`, SDT `0x0011`, EIT `0x0012`, CAT `0x0001` - fixed,
mux-wide, shared by every program (real DVB-SI reserved PIDs, per ETSI EN 300 468). Every other
table is per-program, in a fixed 32-PID block per input's position among the `-i` flags (0-based
index `i`): PMT `0x1000 + i`, video `0x0100 + i*32`, other ES `0x0101 + i*32 ..` in discovery
order, AIT `0x011F + i*32`. With a single `-i` this is identical to before (`i` = 0).

### Multiple inputs (MPTS)

If you apply more than one `-i` input definition, the output will be a Multi Program Transport Stream (MPTS).

Servicing is one poll loop, not one thread per input, taken in round-robin order so no input is
always serviced last. each is capped at 32 TS packets read per tick so one busy source can't
starve the others that tick. There's no de-jitter buffering beyond that. 

Each program keeps forwarding its own source's PCR untouched. There is no synthesized mux-wide
clock, so a program's timing stays accurate to its own source regardless of what the others are doing.

The per-program ES cap is fixed at 31 real streams, one slot always reserved for AIT whether `--hbbtv` is used on that input or not.
Extras beyond that are dropped in discovery order, logged once per program setup as "ES cap (31) reached, dropping N stream(s)".

It's one process, one thread doing the demux/remux/CAS work for every program on a running instance of `dipitvhead`.
Capacity is a CPU budget question, not a thread or connection limit, so plan program count per instance 
(and instance count per host) with that in mind.

### Service info (`-n`, `-s`)

`-s <text>` sets a per-input Service Description Table (SDT)
* Default is a passthrough of that program's own source SDT if present. 
* `-s -` drops the SDT entirely. 

> Note: Going without an SDT might lead to problems with some receiver implementations.

`-n` sets a mux-wide Network Information Table
* SPTS: the default to pass the source's own NIT if present. `-n -` to drop it.
* MPTS: default is no NIT.
* `-n <text>` sets a mux-wide Network Information Table

### EIT (`--strip-eit`)

No EIT reconstruction - source EIT forwarded unless stripped.

SPTS: every packet on the source's EIT pid gets forwarded verbatim (PID remapped, own CC),
whichever services it describes.

MPTS: reassembled into sections, filtered to that program's own service_id (the
source's EIT PID otherwise carries every service in its own multiplex, not just the one being
remuxed), merged onto a shared output EIT.

### Target bitrate (`-b`, `-S`, `-B`)

No `-b`: source rate passes straight through. `-S`: null-packet padding when output falls behind
target. `-B`: paces sending so output never runs ahead of target. Combinable.

### HbbTV signalling (`--hbbtv`)

Injects an AIT (table_id 0x74), one AUTOSTART application: `--hbbtv-org-id`/`--hbbtv-app-id`
identify it, `--hbbtv` is its entry-point URL (`transport_protocol_descriptor`, protocol_id
0x0003). Not a passthrough of any source AIT - that references carousel PIDs we don't carry.

### Identifiers (`--tsid`, `--onid`, `--sid`)

`--tsid`/`--onid` (transport_stream_id/original_network_id): mux-wide, default 1. `--sid`
(service_id, per-input, doubles as the PMT program_number): default auto-assigns the lowest
integer not already used explicitly by another `-i`; explicit duplicates are a startup error.

### Reconnecting (`-e`)

SPTS: no `-e` means any input error stops the tool; `-e <seconds>` reopens after the
delay, you handle restarts yourself.

MPTS: every input always retries independently. Default: 5 seconds.
One input being down never stops output for the others. Output socket and
continuity counters stay up across any gap.

### Live stats (`-v`)

One self-updating line on stderr, about once a second.

## CAS SCS and Scrambler (`--cas-*`)

Acts as a DVB Simulcrypt SCS towards one or more ECMGs (one per `--cas-ecmg`), and as the
EMMG-side MUX towards each one's EMMG client, per ETSI TS 103 197 (protocol versions 2 and 3,
auto-negotiated per vendor unless that vendor's `--cas-ecmg-version` / `--cas-emmg-version`
pins one). Scrambles CISSA (ETSI TS 103 127, 128-bit AES-CBC, needs OpenSSL) or CSA1+2 (need libdvbcsa) 
content on the PIDs listed in `--cas-pids`, exactly once regardless of vendor count.
Every vendor's ECMG gets the same control word. Emits one `CA_descriptor` per vendor plus one
shared `scrambling_descriptor` in the PMT, a CAT with one `CA_descriptor` per vendor, and each
vendor's own ECM (`--cas-ecm-pid`) and EMM (`--cas-emm-pid`) streams.

### Selecting PIDs to scramble (`--cas-pids`)

Comma-separated list, each entry is either an output PID (dec or `0x`-hex) or one of the keywords
`video`/`audio`, meaning every video/audio elementary stream on the output. Freely mixable:

* `--cas-pids video,audio`: all video and audio streams (same as the default)
* `--cas-pids video`: just the video stream
* `--cas-pids 0x0103,video`: PID 0x0103 plus every video stream
* `--cas-pids audio,0x0104,0x0106`: all audio streams plus PIDs 0x0104 and 0x0106

Omit `--cas-pids` entirely and it defaults to `video,audio`. PIDs are given/resolved on the
*output* side (see the remapped PIDs under Codec support above), matched against the source
PMT's `stream_type` once it's known - resolving to nothing (e.g. `video` requested but the
source has no video ES) is a startup error.

With multiple `-i`, `video`/`audio` resolve against *every* program's own discovered ES, so
every input must be discovered within 15s of startup for CAS to start at all - past that,
`dipitvhead` fails fast and names whichever input(s) never made it, rather than start
scrambling with an incomplete pid list or block forever. Numeric PIDs sidestep this entirely
(no discovery dependency, same as single-input).

### Crypto-period timing: PCR or wall-clock

SPTS: crypto-period cadence is driven by the source's own PCR, not wall-clock or a
configured bitrate (VBR support). DVB time is king. `dipitvhead` fails fast if no PCR is
observed on the PCR_PID within a few seconds of startup. PCR discontinuities (splice, failover)
are handled gracefully at runtime, using the wall-clock only as a plausibility fence to reject
bogus jumps.

MPTS: No single program's PCR is trustworthy as the whole mux's clock, so crypto-period cadence
is wall-clock driven instead. It keeps advancing through the all-down steady state, 
unaffected by which programs currently have live data.

### On ECMG loss (`--cas-resilience`)

* `frozen` (default): keeps scrambling with the last known-good CW indefinitely until the ECMG
comes back. The crypto period stays put on whatever it was, the service never blacks out.
* `cycling`: keeps flipping parity on the normal crypto-period schedule, alternating between
the last two known CWs (even/odd), instead of freezing on one.
* `silent`: stops sending this ECM PID once the ECMG becomes unreachable, instead of resending
the last known-good ECM. Content stays scrambled with the last known-good CW, same as `frozen` -
only the ECM stream itself goes quiet.

### EMMG (`--cas-emmg-port`, `--cas-emmg-version`)

dipitvhead is the EMMG-side MUX: it listens (`--cas-emmg-port`, default 8002) and the EMMG
client connects to it, once per `--cas-ecmg` vendor (each with its own `--cas-emmg-port`).
For now, the only topology, not the reversed one where the MUX dials out to the EMMG. Accepts
whichever protocol version the client proposes unless `--cas-emmg-version` is set. EMM
datagrams are queued and drained onto that vendor's own `--cas-emm-pid` on arrival.

### Multi-CAS (`--cas-required`, `--cas-fallback-clear`)

Content is scrambled exactly once, with one shared control word handed to every configured
vendor's ECMG - `--cas-resilience` only decides what a single vendor's own ECM stream does
while its ECMG is unreachable, not whether the content itself keeps playing. That's a
separate, global decision:

* Default: content stays scrambled on the last known-good CW no matter how many vendors are
down, same as `frozen` at the content level - as long as at least one vendor is up, or (if
none are marked `--cas-required`) even if all of them are down.
* `--cas-fallback-clear`: switches that default to clear-to-air (`transport_scrambling_control`
= 00) once every vendor is down, or once any vendor marked `--cas-required` is down
specifically - regardless of whether other, non-required vendors are still up.

### BISS (`--biss1-sw`, `--biss2-sw`, `--biss2-emit-esw`)

`--biss1-sw <hex12>` scrambles with legacy BISS1 Mode 1: a static 12 hex char Session Word, no ECMG/EMMG.
Mutually exclusive with `--biss2-sw`/`--cas-algo`/`--cas-ecmg`. Same `CA_descriptor`/CAT signaling as BISS2 Mode 1/E -
only the cipher (CSA1, not CISSA) differs. BISS1 Mode E (DES) is not supported.

`--biss2-sw <hex32>` scrambles with BISS2 Mode 1/E (EBU Tech 3292) instead: a static 32 hex char Session Word used
directly as the CISSA key, no ECMG/EMMG at all. Mutually exclusive with `--cas-algo`/`--cas-ecmg`.

`--cas-pids` still applies (same PID-selection semantics, default `video,audio`). 
Emits a `CA_descriptor` (`CA_system_id 0x2602`, no real ECM PID) and an empty CAT, no `scrambling_descriptor`.

`--biss2-emit-esw <hex32>` (needs `--biss2-sw`) logs the AES-128-ECB Encrypted Session Word for the given 32 hex 
receiver ID once at startup, for operators distributing the SW to receivers out of band (BISS2 Mode E).

### BISS-CA (`--biss2-ca-receivers`, `--biss2-ca-session-id`)

`--biss2-ca-receivers <dir>` enables BISS2 Mode CA (EBU Tech 3292-s1, `CA_system_id 0x2610`): real
RSA-2048-OAEP + AES-128-CBC key exchange with per-receiver entitlement, in place of a single static SW.
Mutually exclusive with `--cas-algo`/`--cas-ecmg`/`--biss1-sw`/`--biss2-sw`.

`<dir>` holds one PKCS#8 PEM public key per entitled receiver/group. Rescanned on `SIGHUP`; removing a
key revokes that receiver (forces an immediate Session Key change). `--cas-cp-duration` sets the Session
Word rotation period (minimum 1000ms here); the Session Key rotates every 6th SW period.

`--biss2-ca-session-id <n>` sets the administrative `entitlement_session_id` (dec or 0x-hex, 16 bit);
random at startup if omitted.

`--cas-pids` still applies. Emits a real `CA_descriptor` (`CA_system_id 0x2610`, real ECM PID) with a
`bissca_entitlement_session_id_descriptor`, a real (non-empty) CAT, and a `scrambling_descriptor` (CISSA).
ECM/EMM PIDs are auto-allocated.

### Limitations

CISSA, CSA1/CSA2, BISS1 Mode 1, BISS2 Mode 1/E, BISS2 Mode CA.

* No CSA3/CSA-ALT
* No BISS1 Mode E (DES)
* BISS2 Mode CA: no group key pairs, but one keypair per file in receivers-dir.
  No `entitlement_priv_data_loop` vendor extensions, `prevent_descrambled_forward`/
  `prevent_decoded_forward`/`insert_watermark` entitlement flags always 0 (unenforced)

### Dependencies

CAS support is a CMake/configure-time option (`DIPITVHEAD_CAS`, on by default). 
* CISSA needs OpenSSL
* CSA1 and CSA2 need libdvbcsa (`DIPITVHEAD_CSA2` / `HAVE_DVBCSA`). 

Missing either degrades gracefully to "that algorithm unavailable", not a build failure.

Static releases (`.tar.gz`) in this repository do not contain libdvbcsa. 
Build against it yourself if you want CSA1/CSA2 in a static build.


## Signals

* `^C`, SIGINT or SIGTERM: Stop
* SIGHUP: re-read BISS2-CA ceritficates directory (in case you added/removed/changed them)

## Running under systemd

If you intend to run `dipitvhead` as a systemd service, the unit below is a reasonable starting point.
`-e` is included so a dropped SPTS input reopens itself instead of relying on a full process restart.

> Note: This service is a micro SCS/muxer/scrambler. How many of them you want to pack on a host is entirely yours.
> Make sure to do integration tests particularly addressing how the load resulting from this decision is handled.
> Replace `instancename` to make them easier to distinguish.

```ini
[Unit]
Description=dipitvhead-instancename
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/dipitvhead -i rtp://@239.19.75.1:8700 -e 5 -m 239.1.1.1:5000 -s "My Channel"
Restart=on-failure
RestartSec=5
StartLimitIntervalSec=60
StartLimitBurst=10
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes

[Install]
WantedBy=multi-user.target
```

## Examples

```sh
dipitvhead -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 -s "My Channel"

dipitvhead -i https://host/live/x/y.ts -k -m 239.1.1.2:5000 -b 8000 -S -B

dipitvhead -i udp://@239.0.0.1:5000 -m 239.5.5.5:6000 \
  --hbbtv https://example.org/hbbtv/ --hbbtv-org-id 1 --hbbtv-app-id 100

# MPTS: two independent SPTS sources merged into one output, each its own program.
# --sid/--sdt pair with the -i right before them.
dipitvhead -i udp://@239.0.0.1:5000 --sid 101 --sdt "Channel One" \
           -i udp://@239.0.0.2:5001 --sid 102 --sdt "Channel Two" \
           -m 239.5.5.5:6000

# enigma2 DVB-S2 MPTS source (would need "-p" if you don't just want _any_ channel)
dipitvhead -i http://receiver:8001/1:0:10:10:3EF:1:C00000:0:0:0: -m 239.5.5.5:6000

# enigma2 + oscam relay: MPTS, live program picked automatically
dipitvhead -i http://receiver:17555/1:0:CA:CA:C:85:C00000:0:0:0: -m 239.5.5.6:6000

# scramble an SPTS and re-stream it
dipitvhead -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 \
  --cas-algo cissa --cas-ecmg tcp://ecmg.example:2222 --cas-super-id 0x4A750002 --cas-ecm-id 1 --cas-pids 0x0100,0x0101

# multi-CAS: two vendors, one required. --cas-ecmg opens a slot; the flags after it
# (version/super-id/ecm-id/pid/resilience/required) pair with that --cas-ecmg.
dipitvhead -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 --cas-algo cissa \
  --cas-ecmg tcp://ecmg-a.example:2222 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
             --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port 8002 --cas-required \
  --cas-ecmg tcp://ecmg-b.example:2222 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
             --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port 8003 --cas-resilience silent \
  --cas-pids 0x0100,0x0101 --cas-fallback-clear

# BISS2 Mode 1/E: no ECMG/EMMG needed
dipitvhead -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 --biss2-sw 00112233445566778899aabbccddeeff --cas-pids video,audio

# legacy BISS1 Mode 1: 12 hex char Session Word, CSA1
dipitvhead -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 --biss1-sw 0123456789ab --cas-pids video,audio

# BISS2 Mode CA: per-receiver RSA entitlement, one pubkey per file under the dir
dipitvhead -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 --biss2-ca-receivers /etc/biss-ca/receivers --cas-pids video,audio

# transcoding: not our job, but ffmpeg pipes straight in
ffmpeg -i <source> -c:v libx264 -c:a aac -f mpegts - | dipitvhead -i - -m 239.5.5.5:6000
```
