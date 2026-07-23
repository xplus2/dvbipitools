# dipiradiohead

IPI Radio Headend

Fetches an Icecast/Shoutcast stream and re-muxes it as a DVB-IPI multicast. No transcoding.

```
dipiradiohead -i <uri> -m <mcast>:<port> [options]
```

## Options

| flag | long form | argument | default |
|---|---|---|---|
| `-i` | `--input` | `<uri>` | required |
| `-m` | `--mcast` | `<group>:<port>` / `[<group6>]:<port>` | required |
| `-I` | `--iface` | `<iface>` | kernel route |
| `-r` | `--rtp` | - | off (plain UDP) |
| `-T` | `--ttl` | `<n>` | 1 (kernel default) |
| `-n` | `--nit` | `<text>` | none |
| `-s` | `--sdt` | `<text>` | `dipiradiohead` |
| `-e` | `--error` | `<seconds>` | fail once, no retry |
| `-k` | `--insecure` | - | off (TLS verified) |
| | `--tsid` | `<n>` | 1 |
| | `--onid` | `<n>` | 1 |
| | `--sid` | `<n>` | 1 |
| `-v` | `--verbose` | - | off |
| | `--color` | `auto\|always\|never` | `auto` |
| `-h` | `--help` | - | |

## Input (`-i`)

`http://` or `https://`. Codec: mp3, mp2, AAC ADTS, AAC LATM/LOAS, auto-detected from stream sync bytes.

`https://` verifies the cert chain, hostname and expiry by default; `-k` skips all three (self-signed lab/test sources).

HTTPS support is a build-time option (`-DDIPIRADIOHEAD_TLS=OFF`, or auto-off if OpenSSL isn't
found); a build without it fails cleanly on any `https://` source instead of connecting.

Response body sniff (not URL suffix):
* audio (ID3 tag or MPEG/ADTS/LATM sync at offset 0) -> used as-is.
* M3U (`#EXTM3U` or bare `http(s)://` line) -> first URL line followed.
* PLS (`[playlist]`, `FileN=<url>`) -> first `FileN=` followed.

Max 5 playlist hops, each re-sniffed.

## Output (`-m`, `-I`, `-r`)

`-m <group>:<port>` / `-m [<group6>]:<port>`. `-I` sets the outgoing interface (default: kernel
route). `-r` wraps output in RTP, matching `dipirec -i rtp://`; without it, plain UDP, matching
`-i udp://`. 7 TS packets (1316 B) per datagram either way. `-T` sets the multicast TTL / hop
limit (default 1, i.e. link-local only - raise it to route beyond the first hop).

## Now-playing metadata

EIT present event (table 0x4E, no following), text = `[artist] [title]` / `[artist]` / `[title]`.

Source, auto-detected per stream:
* `icy-metaint` header present -> ICY: `StreamTitle='...'` blocks parsed + stripped at that byte
  interval. Splits on first `" - "` into artist/title; no split found -> title only.
* no `icy-metaint` -> inline ID3v2: `ID3` sync checked only at audio frame boundaries (never
  mid-frame). `TIT2`/`TPE1` (v2.3+v2.4, text or UTF-16) read, tag stripped from the ES.

Repetition: PAT/PMT 100ms, SDT 2s, NIT 10s (only if `-n` set), EIT 1s or immediately on change.
Fixed PIDs: PAT 0x0000, NIT 0x0010, SDT 0x0011, EIT 0x0012, PMT 0x0100, audio 0x0101.

## Service info (`-n`, `-s`)

`-n` NIT `network_name`. `-s` SDT `service_name` (provider name is fixed: `dipiradiohead`). UTF-8.

## Identifiers (`--tsid`, `--onid`, `--sid`)

transport_stream_id / original_network_id / service_id, default 1. Only matters with several
instances on one network.

## Reconnecting (`-e`)

No `-e`: any fetch error stops the tool. `-e <seconds>`: reopens the input after the delay;
multicast socket, continuity counters and PSI versions stay up across the gap.

## Live stats (`-v`)

One self-updating line on stderr, about once a second.

## Stopping

`^C`, SIGINT or SIGTERM.

## Examples

```sh
dipiradiohead -i https://orf-live.ors-shoutcast.at/oe1-q2a.m3u -m 239.1.1.1:5000 -r -s "OE1"

dipiradiohead -i http://radio886.at/streams/radio_88.6/aac -m 239.1.1.2:5000 -e 5

dipiradiohead -i http://onair.krone.at/kronehit.mp3 -m 239.5.5.5:6000 &
dipirec -i udp://@239.5.5.5:6000 -o kronehit.mka
```
