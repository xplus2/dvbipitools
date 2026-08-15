# dipirec

Records or replays a DVB-IPI stream to a file, stdout, multicast, or an RTMP(S) ingest server.

```
dipirec -i <uri> -o <target> [options]
```

## Options

| flag | long form         | argument              | default                       |
|------|-------------------|-----------------------|-------------------------------|
| `-i` | `--in`            | `<uri>`               | required                      |
| `-o` | `--out`           | `<target>`            | required, repeatable          |
| `-a` | `--audio`         | `<track>` / `all`     | `all`                         |
| `-f` | `--format`        | `raw\|ts\|mkv\|mka`   | from `-o` suffix, else `ts`   |
| `-p` | `--pmt-pid`       | `<pid>` / `all`       | none (see below)              |
| `-s` | `--subtitles`     | `strip\|keep\|srt`    | `keep`                        |
| `-t` | `--time`          | `<duration>`          | no limit (runs until stopped) |
| `-I` | `--iface`         | `<iface>`             | kernel route                  |
| `-O` | `--out-iface`     | `<iface>`             | kernel route                  |
|      | `--ttl`           | `<n>`                 | kernel default (`1`)          |
| `-v` | `--verbose`       |                       | off                           |
|      | `--sub-lead`      | `<ms>`                | `1000`                        |
|      | `--color`         | `auto\|always\|never` | `auto`                        |
|      | `--ret`           | `<addr>:<port>`       | off (no gap repair)           |
|      | `--no-ret-mc`     |                       | off (joins repair session)    |
|      | `--ret-mc-port`   | `<port>`              | same as `-i`'s port           |
|      | `--ret-pt`        | `<n>`                 | `99`                          |
|      | `--ret-wait`      | `<ms>`                | `200`                         |
|      | `--pace`          |                       | off (file/stdin source only)  |
|      | `--strip`         | `<list>` / `none`     | `NUL,NIT,AIT,EIT`             |
|      | `--profile`       | `simple\|main`        | `simple` (`-o rist://` only)  |
|      | `--secret`        | `<psk>`               | none (`-o rist://` only)      |
|      | `--cname`         | `<name>`              | library default (`-o rist://` only) |
|      | `--buffer`        | `<ms>`                | library default (`-o rist://` only) |
|      | `--insecure`      |                       | off (`-o rtmps://` only)      |
| `-h` | `--help`          |                       |                               |

## Input (`-i`)

| schema                                       | what's this?                   |
|----------------------------------------------|--------------------------------|
| `rtp://@<group>:<port>`                      | RTP wrapped SPTS or MPTS       |
| `udp://@<group>:<port>`                      | plain SPTS or MPTS             |
| `http://<host>:<port>/<cmd>/<group>:<port>/` | udpxy, `cmd` is `rtp` or `udp` |
| `-`                                          | stdin, TS or RTP wrapped TS    |
| `<path>`                                     | a file, TS or RTP wrapped TS   |

`@` is optional. `<group>` can be an IPv4 or IPv6 multicast address.

For `rtp://` and `udp://` the tool joins the group itself (IGMPv2 / MLD, any source) and leaves on exit.
RTP headers are detected and removed automatically, so a source that is actually plain TS works even when given as `rtp://`.

`-i -` reads stdin, `-i <path>` reads a file - anything not matching `rtp://`/`udp://`/`http://` is treated
as a path. Both cases auto-detect RTP-wrapped vs. plain TS from the first bytes: fine either way, no need to
know which one a captured dump actually is. Ambiguous or too-short input falls back to plain TS.

The source can be a single-program stream (SPTS) or a multi-program mux (MPTS, e.g. one produced by
`dipitvhead`'s own multi-`-i` output). See `-p` below for how an MPTS is handled.

## Real-time pacing (`--pace`)

`-i -`/`-i <path>` read as fast as the file/pipe allows, not at broadcast speed - fine for a straight
conversion, not fine for `-o -` into something that expects a live-ish feed and can't absorb a burst.

`--pace` throttles reading to the source's own original timing: the RTP timestamp when the input turned
out to be RTP-wrapped, the stream's own PCR otherwise (once the PMT resolves - nothing to pace against
before that). Only valid with `-i -`/`-i <path>`; rejected on a network source, which is already real-time.

A jump of more than 2 seconds between expected and actual time (a real discontinuity, not scheduler
jitter) resyncs instead of trying to sleep-catch-up.

## MPTS input (`-p`)

`dipirec` waits for the PAT and checks how many programs the source actually carries.

* **SPTS** (one program): `-p` is ignored - a warning is logged if it was given
* **MPTS**:
  * `-p <pid>` given: pins that one program's PMT pid, then everything else works as if it were
     an SPTS (all formats, `-a`/`-s` included). Rejected if that pid isn't actually in the PAT.
  * `-p all` given: record every program at once.
    * `-f raw`/`-f ts`: forwarded unfiltered - with nothing selected there's nothing left to filter.
    * `-f mka`: every program's audio becomes its own track (forces `-a all`, since a single `-a N` has no
      coherent meaning across differently-numbered programs), each track labeled with that program's own
      SDT name once it arrives.
    * `-f mkv`: rejected. Matroska has no way to say which audio track belongs to which of several video
      tracks (there's no such association in the format - yet), so `-f mkv` always needs a single `-p <pid>`.
  * neither given: fails early, after a brief wait for each program's SDT name, listing the
    available `sid`/PMT pid/name so you can choose. A program whose name never arrives in that window is
    listed as `(no SDT)` rather than blocking further.

## Output (`-o`)

Repeatable: a file plus one or more RTMP(S) pushes, or several RTMP(S) targets at once.

| schema                              | what's this?                                    |
|--------------------------------------|--------------------------------------------------|
| `<path>`                             | a file                                            |
| `-`                                   | stdout, also pipeable into [dipidescramble](../dipidescramble/README.md) |
| `rtp://@<group>:<port>`              | RTP-wrapped multicast, `-f raw`/`ts` only         |
| `udp://@<group>:<port>`              | plain multicast, no RTP header, `-f raw`/`ts` only |
| `rist://<host>:<port>[?query]`       | single RIST peer, requires librist, `-f raw`/`ts` only |
| `rtmp://<host>[:port]/<app>/<key>`   | RTMP publish, default port `1935`                 |
| `rtmps://<host>[:port]/<app>/<key>`  | RTMP over TLS, default port `443`                 |

`rtp://`/`udp://` join nothing, they just send: 7-packet (1316-byte) datagrams, RTP-wrapped with
a fresh SSRC/sequence/timestamp for `rtp://`, plain for `udp://`. `rist://` sends the same
1316-byte chunks through librist (own framing, no RTP), one peer, not bonded. See
[dipirist](../dipirist/README.md) for bonding. `--profile`/`--secret`/`--cname`/`--buffer`
configure it (`--secret` requires `--profile main`). Combined with
[`--pace`](#real-time-pacing---pace): replay a file back onto multicast at its original speed.

### RTMP(S)

RTMP output ignores `-f`: H.264/HEVC video. Unsupported video (MPEG-2) or audio
(MP2) is dropped from that push.

`-f raw` can't be combined with an `rtmp(s)://` target. `-f
mkv`/`mka` can, given exactly one plain file target for the Matroska mux itself. `-p all` can't,
same reason as `-f mkv`: RTMP is one program.

`--insecure` skips cert/hostname/expiry checks, for `rtmps://`. 

A push target reconnects on its own on a drop, other `-o` targets keep going regardless. After a
(re)connect it waits for the next keyframe, same as any live encoder joining mid-GOP.

## Formats (`-f`)

If `-f` is omitted the format is taken from the `-o` suffix (`.ts`, `.mkv`, `.mka`) and otherwise defaults to `ts`.

| format   | description                                        |
|----------|----------------------------------------------------|
| `raw`    | RTP unwrapped, SPTS (stuffing and all tables kept) |
| `ts`     | SPTS, cleaned up (see below)                       |
| `mkv`    | Matroska video, audio and optional subtitles       |
| `mka`    | Matroska audio                                     | 

### TS Stripper (`--strip`)

`-f ts` drops a configurable set of tables/pids. Default, with no `--strip` given: `NUL,NIT,AIT,EIT`.

| token  | what                                                                                        |
|--------|---------------------------------------------------------------------------------------------|
| `NUL`  | CBR stuffing (null packets, pid 0x1FFF)                                                     |
| `NIT`  | network info table, and its entry in the PAT                                                |
| `AIT`  | application signalling table, and its PMT ES entry                                          |
| `EIT`  | event info (EPG)                                                                            |
| `CAT`  | conditional access table                                                                    |
| `ECM`  | entitlement control messages, and the PMT's ES-level CA descriptor referencing them         |
| `EMM`  | entitlement management messages, and the PMT's program-level CA descriptor referencing them |
| `RST`  | running status table                                                                        |
| `TDT`  | time/date table - shares a pid with TOT, either token drops both                            |
| `TOT`  | time offset table - shares a pid with TDT, either token drops both                          |
| `INT`  | IP/MAC notification table (identified by table_id, not a fixed pid)                         |

`--strip none` disables stripping entirely.
Given `--strip` with any format other than `-f ts`, it's ignored with a warning.

What always survives: `PAT`/`PMT` (rewritten to drop whatever their own entries pointed at, but there's no
TS without them), `SDT`, and every `PES` stream.

What's changed regardless: continuity counters, CRC32 recalculation on any rewritten section.


### Matroska details

Video doesn't get transcoded. MPEG2, H.264 (`V_MPEG4/ISO/AVC`) and HEVC (`V_MPEGH/ISO/HEVC`) get handled properly.

Audio covers AC3, E-AC3, MPEG layer 1/2/3, AAC (ADTS) and AAC_LATM. Each audio track keeps the ISO 639
language from the PMT, or `und` when the stream does not signal one.

> TV stations and IPTV providers are creative on audio flagging. Who cares about ISO 639-2 anyway. Do not wonder if ...
> - "OLA" is used for "original language, we just dump what we've got, deal with it"
> - "MLT" is not Maltese, but the audio-description for visually impaired

Some metadata gets set, like SDT and NIT info, date and time.
Most MKV metadata reader implementations won't show most of it anyway. 

Recording starts at the first video keyframe, so the file opens on a decodable picture; 
audio before that point is dropped to keep the start aligned. Files have no `Duration` element because it is written 
as a live stream; players compute the length from the content.

## Audio track selection (`-a`)

`-a all` (the default) keeps every audio track. `-a <n>` keeps only track `n`, counted from 1 in PMT order.
If the stream has fewer tracks the tool reports a mismatch and exits.

## Subtitles (`-s`)

| mode    | effect                                                                       |
|---------|------------------------------------------------------------------------------|
| `keep`  | default; teletext and DVB subtitle streams are passed through in `ts`        |
| `strip` | those streams are removed, including from the PMT                            |
| `srt`   | teletext subtitles are decoded and stored as an SRT track (`mkv`/`mka` only) |

`-s srt` uses the subtitle page advertised in the teletext descriptor.
DVB bitmap subtitles are not converted, no OCR here.

Teletext subs are transmitted after the speech they describe, so they run late.
`--sub-lead <ms>` shifts every cue earlier; the default is 1000 ms. Use `--sub-lead 0` to keep the broadcast timing.
A cue held on screen with no following subtitle stays for at least 1.2 s and at most 5 s, since teletext
carries no signal for clearing the screen.

## RET repair (`--ret`)

Optional `--ret <addr>:<port>` points at an ETSI TS 102 034 Annex F (like [dipifccret](../dipifccret/README.md))  edge server's address.

If set, `-i rtp://` gap detection kicks in: a hole in the RTP sequence gets one NACK sent to that address, 
and whatever repair comes back (unicast reply, or the multicast repair session per Annex F.6.2.2) gets spliced 
back into the recording in order.

No RSI/SD&S discovery for this (like `dipisds` would send), so address+port have to be known and passed explicitly.

Off by default. Without `--ret` nothing changes, no added latency, no new sockets.

| flag                   | effect                                                                             |
|------------------------|------------------------------------------------------------------------------------|
| `--no-ret-mc`          | skip joining the RET server's multicast repair session, unicast reply only         |
| `--ret-mc-port <port>` | repair session port, if it differs from `-i`'s (default: same port)                |
| `--ret-pt <n>`         | RTX payload type, must match the RET server's `-R` (default: 99)                   |
| `--ret-wait <ms>`      | how long to hold a gap open waiting for the repair before giving up (default: 200) |

A gap gets exactly one NACK, no retries. Past `--ret-wait`, the hole is let through.
This trades a small amount of latency for a chance at a complete recording, while it's not a guarantee.

## Recording duration (`-t`)

A plain number is seconds. Also accepted: `90`, `5m`, `5m30s`, `1h`, `1h3m`, `1h3m20s`, `10:20` (minutes:seconds) and `01:20:03`
(hours:minutes:seconds, hours may exceed 24). 
Without `-t` the recording runs until stopped.

## Network interface (`-I`, `-O`, `--ttl`)

`-I` picks the interface for `-i`'s multicast join. `-O`/`--out-iface` picks the interface for
`-o rtp://`/`-o udp://`'s multicast send; ignored (with a warning) if `-o` isn't one of those.
Without them, the kernel's default multicast route is used, which is usually wrong in multi-homing.
They're independent, so a box bridging two segments can join on one NIC and send on the other.

`--ttl <n>` sets the TTL (IPv4) / hop limit (IPv6) on `-o rtp://`/`-o udp://` packets; default is
the kernel's (`1`, i.e. link-local only). Also ignored (with a warning) outside `-o rtp://`/`-o udp://`.
Needed for a replay to cross a router - `1` won't leave the sending segment.

## Live stats (`-v`)

Prints a single, self updating line to stderr about once a second.

## Signals

`^C` SIGINT or SIGTERM stops the recording, closes the output properly and leaves the multicast group.

## Examples

```sh
# 30 minutes to a transport stream
dipirec -i rtp://@239.19.75.1:8700 -o show.ts -t 30m -I eth0

# Matroska, second audio track only, subtitles as SRT
dipirec -i rtp://@239.19.75.1:8700 -o show.mkv -a 2 -s srt

# radio to Matroska audio
dipirec -i udp://@239.0.175.1:8700 -o radio.mka -t 1h

# untouched transport stream through a udpxy gateway
dipirec -i http://10.0.0.1:4022/rtp/239.19.75.1~8700 -o dump.ts -f raw

# pipe to another tool
dipirec -i rtp://@239.19.75.1:8700 -o - -f ts | ffplay -

# with RET gap repair against a dipifccret edge server
dipirec -i rtp://@239.19.75.1:8700 -o show.ts --ret 10.0.0.1:6000

# MPTS source: pin one program
dipirec -i rtp://@239.1.1.3:5000 -p 0x1000 -o show.ts

# MPTS source: every program's audio into one MKA
dipirec -i rtp://@239.1.1.3:5000 -p all -o all_channels.mka

# replay a file at its own original speed, into something that expects a live feed
dipirec -i show.ts --pace -o - -f ts | some-live-consumer

# replay a filtered recording back onto multicast, at its own original speed
dipirec -i show.ts --pace -o rtp://@239.9.9.9:6000 -O eth1 --ttl 16

# from stdin, also strip CAT/ECM/EMM on top of the default set
dipirec -i - -o show.ts --strip NUL,NIT,AIT,EIT,CAT,ECM,EMM < capture.ts

# push live to an RTMP ingest server
dipirec -i rtp://@239.19.75.1:8700 -o rtmp://live.example.com/app/key

# record to disk and push live at the same time, self-signed ingest cert
dipirec -i rtp://@239.19.75.1:8700 -o show.mkv -o rtmps://ingest.example.com/app/key --insecure
```
