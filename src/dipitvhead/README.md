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
| `-I` | `--iface` | `<iface>` | kernel route |
| `-r` | `--rtp` | - | off (plain UDP) |
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
| `-h` | `--help` | - | |

## Input (`-i`)

`udp://`, `rtp://`, `http://`, `https://`, `-` for stdin. RTP headers stripped automatically.
HTTPS: build-time option (`-DDIPITVHEAD_TLS=OFF`, auto-off without OpenSSL), `-k` skips cert
verification.

## Program selection (`-p`)

PAT watched on startup and logged. No `-p`: first PAT-listed program whose PMT actually arrives
wins (real MPTS sources often list many services, stream one). `-p <pid>` forces a PMT PID.

## Codec support

Video: MPEG-2, H.264, HEVC. Audio: MPEG-1/2 (layer 1/2/3), AC-3, E-AC-3, AAC (ADTS/LATM).
Subtitles: EBU teletext, DVB bitmap. Everything else (carousels, SCTE-35, CA/ECM) dropped.
Output PIDs: PAT `0x0000`, PMT `0x1000`, video `0x0100`, other ES `0x0101..` in discovery order,
NIT `0x0010`, SDT `0x0011`, EIT `0x0012`, AIT `0x0020`.

## Service info (`-n`, `-s`)

Default: passthrough the source's own NIT/SDT text if present. `<text>`: our own (provider name
fixed to `dipitvhead`). `-`: drop the table outright.

## EIT (`--strip-eit`)

No EIT reconstruction - source EIT forwarded verbatim (PID remapped, own CC) unless stripped.

## Target bitrate (`-b`, `-S`, `-B`)

No `-b`: source rate passes straight through. `-S`: null-packet padding when output falls behind
target. `-B`: paces sending so output never runs ahead of target. Combinable.

## HbbTV signalling (`--hbbtv`)

Injects an AIT (table_id 0x74), one AUTOSTART application: `--hbbtv-org-id`/`--hbbtv-app-id`
identify it, `--hbbtv` is its entry-point URL (`transport_protocol_descriptor`, protocol_id
0x0003). Not a passthrough of any source AIT - that references carousel PIDs we don't carry.

## Identifiers (`--tsid`, `--onid`, `--sid`)

transport_stream_id / original_network_id / service_id, default 1. `--sid` doubles as the PMT
program_number.

## Reconnecting (`-e`)

No `-e`: any input error stops the tool. `-e <seconds>`: reopens after the delay, output socket
and continuity counters stay up across the gap.

## Live stats (`-v`)

One self-updating line on stderr, about once a second.

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

# transcoding: not our job, but ffmpeg pipes straight in
ffmpeg -i <source> -c:v libx264 -c:a aac -f mpegts - | dipitvhead -i - -m 239.5.5.5:6000
```
