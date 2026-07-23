# dipirec

Records a DVB-IPI stream to a file or to stdout.

```
dipirec -i <uri> -o <path> [options]
```

## Input (`-i`)

| schema                                   | what's this?                       |
|----------------------------------------------|--------------------------------|
| `rtp://@<group>:<port>`                      | RTP wrapped SPTS               |
| `udp://@<group>:<port>`                      | plain SPTS                     |
| `http://<host>:<port>/<cmd>/<group>:<port>/` | udpxy, `cmd` is `rtp` or `udp` |

`@` is optional. `<group>` can be an IPv4 or IPv6 multicast address.

For `rtp://` and `udp://` the tool joins the group itself (IGMPv2 / MLD, any source) and leaves on exit.
RTP headers are detected and removed automatically, so a source that is actually plain TS works even when given as `rtp://`.

## Output (`-o`)

A file path, or `-` for stdout. Don't worry, status outout and `-v` alwayxs go to stderr.

## Formats (`-f`)

If `-f` is omitted the format is taken from the `-o` suffix (`.ts`, `.mkv`, `.mka`) and otherwise defaults to `ts`.

| format   | description                                        |
|----------|----------------------------------------------------|
| `raw`    | RTP unwrapped, SPTS (stuffing and all tables kept) |
| `ts`     | SPTS, cleaned up (see below)                       |
| `mkv`    | Matroska video, audio and optional subtitles       |
| `mka`    | Matroska audio                                     | 

### TS Stripper

The following get removed from TS:
* CBR stuffing (null packet)
* NIT (network information table)
* AIT (it's sort of a trojan anyway)
* EIT (now/next. does anyone besides the French use it in IPI?)
* CAT, CA/ECM (not that it would be there. It's usually on a second port)

What survives:
* PAT/PMT - rewritten, but there's no TS without
* SDT - service description table (usually the tv station name)
* Video/Audio elementary stream

What's changed:
* Continuity counters
* CRC32 recalculation


### Matroska details

Video doesn't get transcoded. H.264 (`V_MPEG4/ISO/AVC`) and HEVC (`V_MPEGH/ISO/HEVC`) get handled properly.
Possibly, MPEG2 will work too, but finding it in the wild today is a bit of a challenge.

Audio covers AC3, E-AC3, mpeg layer 1/2/3, AAC (ADTS) and AAC_LATM. Each audio track keeps the ISO 639
language from the PMT, or `und` when the stream does not signal one.

> TV stations and IPI providers get creative on audio flagging. Who cares about ISO 639-2 anyway. Do not wonder if ...
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

| mode | effect |
| --- | --- |
| `keep` | default; teletext and DVB subtitle streams are passed through in `ts` |
| `strip` | those streams are removed, including from the PMT |
| `srt` | teletext subtitles are decoded and stored as an SRT track (`mkv`/`mka` only) |

`-s srt` uses the subtitle page advertised in the teletext descriptor.
DVB bitmap subtitles are not converted, no OCR here.

Teletext subs are transmitted after the speech they describe, so they run late.
`--sub-lead <ms>` shifts every cue earlier; the default is 1000 ms. Use `--sub-lead 0` to keep the broadcast timing.
A cue held on screen with no following subtitle stays for at least 1.2 s and at most 7 s, since teletext
carries no signal for clearing the screen.

## RET repair (`--ret`)

Optional `--ret <addr>:<port>` points at a [dipiret](../dipiret/README.md) edge server's `-l` address.
If set, `-i rtp://` gap detection kicks in: a hole in the RTP sequence gets one NACK sent to that address, 
and whatever repair comes back (unicast reply, or the multicast repair session per Annex F.6.2.2) gets spliced 
back into the recording in order.
No RSI/SD&S discovery for this (`dipisds` can send it), so address+port have to be known and passed explicitly.

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

## Network interface (`-I`)

Picks the interface for the multicast join. Without it, the kernel's default multicast route is used,
which is usually wrong in multi-homing.

## Live stats (`-v`)

Prints a single, self updating line to stderr about once a second.

## Stopping

`^C` SIGINT or SIGTERM stops the recording, closes the output properly and leaves the multicast group.

## Examples

```sh
# 30 minutes to a transport stream
dipirec -i rtp://@239.2.24.1:8208 -o show.ts -t 30m -I eth0

# Matroska, second audio track only, subtitles as SRT
dipirec -i rtp://@239.2.24.1:8208 -o show.mkv -a 2 -s srt

# radio to Matroska audio
dipirec -i udp://@239.0.144.1:8208 -o radio.mka -t 1h

# untouched transport stream through a udpxy gateway
dipirec -i http://10.0.0.1:4022/rtp/239.2.24.1~8208 -o dump.ts -f raw

# pipe to another tool
dipirec -i rtp://@239.2.24.1:8208 -o - -f ts | ffplay -

# with RET gap repair against a dipiret edge server
dipirec -i rtp://@239.2.24.1:8208 -o show.ts --ret 10.0.0.1:6000
```
