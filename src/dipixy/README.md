# dipixy

When parts of ETSI TS 102 905 (DVB-HN) meet udpxy and lean towards DVB-I, `dipixy` is the result.

It takes various input streams and playlists, allows playback using HLS (ts and fMP4), LL-HLS,
MPEG-DASH (regular and CMAF Low-Latency), progressive HTTP/TS and progressive MP4.

Optionally, it's also a DLNA MediaServer for your home network to consume streams without an additional Set-Top-Box.


Usage:
```sh
dipixy [-l addr:port] [-i source ...] [options]
```

## Options

| flag | long form                | argument              | default                                                 | scope |
|------|--------------------------|-----------------------|---------------------------------------------------------|-------|
| `-I` | `--iface`                | `<iface>`             | kernel default route                                    |       |
| `-l` | `--listen`               | `<addr>:<port>`       | `all:9080`                                              |       |
| `-L` | `--listen-tls`           | `<addr>:<port>`       | `all:9443`                                              |       |
|      | `--tls-cert`             | `<path>`              | search default paths, see below                         |       |
|      | `--tls-key`              | `<path>`              | search default paths, see below                         |       |
| `-j` | `--workers`              | `-1\|-2\|-3` or `<n>` | `-1` (that many x cpu cores) or <n> threads             |       |
| `-c` | `--max-clients`          | `<n>`                 | `256`; cap on concurrent streams                        |       |
|      | `--max-channels`         | `<n>`                 | `32`; cap on concurrent (source,filter,pmt,container)   |       |
|      | `--capture-ring-size`    | `<KiB>`               | `4096`; per-source ingress ring buffer                  |       |
| `-i` | `--input`                | `<source>`            | none, repeatable                                        |       |
| `-n` | `--name`                 | `<name>`              | none; names the input                                   | input |
|      | `--media-type`           | `radio\|tv`           | `tv`; only needed for DLNA                              | input |
| `-k` | `--insecure`             |                       | off; skip TLS input verification                        |       |
|      | `--sds-timeout`          | `<seconds>`           | `3`; sds:// discovery wait                              |       |
|      | `--sds-refresh-interval` | `<seconds>`           | `30`; sds:// retry                                      |       |
|      | `--segment-size`         | `<seconds>`           | `3` (hls, hls-fmp4, llhls, dash, lldash)                |       |
|      | `--segment-count`        | `<n>`                 | `4`                                                     |       |
|      | `--hls-part-size`        | `<seconds>`           | `0.35` (llhls, must be `<` segment-size)                |       |
|      | `--dash-part-size`       | `<seconds>`           | `0.333` (`lldash`, must be `<` segment-size)            |       |
|      | `--dash-utc-url`         | `<url>`               | `http://time.akamai.com/?iso&ms` (`lldash`)             |       |
|      | `--hls-seg-pool`         | `<n>`                 | `8`; per-size-class segment buffer cap                  |       |
|      | `--metrics`              | `<path>`              | `/run/dvbipitools/metrics.sock`                         |       |
|      | `--metrics-id`           | `<name>`              | none (metrics disabled unless set)                      |       |
|      | `--metrics-interval`     | `<s>`                 | `5`                                                     |       |
|      | `--metrics-http`         |                       | off (also serve `/metrics` ourselves)                   |       |
|      | `--status-tpl`           | `<path>`              | built-in, override with your own                        |       |
|      | `--auth`                 | `<user:pass>`         | off; HTTP Basic Auth for status                         |       |
|      | `--cors-origin`          | `<list>`              | `*` (comma-separated allowlist vs `Origin`)             |       |
| `-f` | `--format`               | `<list>`              | all; allow `ts,spts,rawaudio,mp4,hls,llhls,dash,lldash` |       |
|      | `--no-url-rtp`           |                       | off (deactivate `/rtp/`)                                |       |
|      | `--no-url-udp`           |                       | off (deactivate `/udp/`)                                |       |
|      | `--no-url-srt`           |                       | off (deactivate `/srt/`)                                |       |
|      | `--no-pid-filters`       |                       | off (deactivate `?filter=`)                             |       |
|      | `--no-fcc`               |                       | off (ignore SDS fcc)                                    |       |
|      | `--no-ret`               |                       | off (ignore SDS ret)                                    |       |
|      | `--no-status`            |                       | off (deactivate `/ui/status.js`)                        |       |
| `-d` | `--daemonize`            |                       | off (fork to background after startup)                  |       |
| `-v` | `--verbose`              |                       | off                                                     |       |
|      | `--color`                | `auto\|always\|never` | `auto`                                                  |       |
| `-h` | `--help`                 |                       |                                                         |       |

### Related to DLNA/UPnP-AV

| flag | long form                | argument              | default                                  | scope |
|------|--------------------------|-----------------------|------------------------------------------|-------|
|      | `--enable-dlna`          |                       | off (serve SSDP + a UPnP MediaServer)    |       |
|      | `--ssdp-ttl`             | `<n>`                 | `3`                                      |       |
|      | `--ssdp-iface`           | `<iface>`             | kernel default route; interace name      |       |
|      | `--ssdp-interval`        | `<seconds>`           | `60`; NOTIFY re-announce period          |       |
|      | `--ssdp-max-age`         | `<seconds>`           | `1800`; max-age, >= 2x interval          |       |
|      | `--dlna-host`            | `<host>[:<port>]`     | `--listen`, if not 0.0.0.0               |       |
|      | `--dlna-name`            | `<name>`              | `dipixy (<dlna-host>)`                   |       |
|      | `--dlna-keep-multicast`  |                       | off (force DVB-HN 9.2 multicast passing) |       |

---

## Input (`-i`)

`-i` is repeatable and dipixy dispatches each occurrence by the form of its argument:

| form                         | what it is                                        |
|------------------------------|---------------------------------------------------|
| `-`                          | stdin, served at `/stdin/<fmt>`. At most one.     |
| `rist://@host:port`          | RIST input, served at `/rist/<fmt>`. At most one. |
| `sds://addr:port`            | live SD&S/DVBSTP discovery                        |
| `http://url`, `https://url`  | single progressive TS URI                         |
| `*.m3u`/`*.m3u8`             | M3U playlist                                      |
| `*.xspf`                     | XSPF playlist                                     |
| `*.csv`                      | CSV playlist                                      |
| `*.xml`                      | SD&S BroadcastDiscovery TVA XML                   |

M3U, XSPF, and CSV are not strictly limited to the output of `dipiscan`.

Every `sds://`, playlist, and `http(s)://` source shares one index space, numbered in the order
it was given on the command line, so the first of these three kinds is list 1, the 2nd is list 2, ...

`rist://@`, `-` (stdin), or `http(s)://` each define just a single stream, not a playlist. 

`-n <name>` right after an `-i` gives that input a name, usable in URLs instead of its
`/list/<n>/` index or its bare `/rist/`/`/stdin/` route (see URL Paths below). Rules: no `/`, no
leading `.`, not one of `rtp`, `udp`, `srt`, `rist`, `stdin`, `list`, `metrics`, `ui`, `api`,
`dlna`, `export`, and unique across every named `-i`.

## URL Paths

There is a shortcut: Start `dipixy` with all your inputs and open `http://127.0.0.1:9080/`.
The "URL Builder" lets you craft them easily.

The included web interface is as basic, but: 
* It's compiled-in.
* Any web-based video player for HLS/LL-HLS/DASH/LL-DASH would still hit some codec walls (AC3, E-AC3).
* By staying a static website that's easy to read, it also functions as a kind of template if you want to roll your own.
  Using `--status-tpl`, you are invited to roll your own replacement. `SIGHUP` will reload it at runtime.


Anyway, here are the details if you want to craft your URIs manually:

These routes address a source directly (`fmt` is one of `ts`, `spts`, `rawaudio`, `mp4`, `hls`, `hls-fmp4`, `llhls`, `dash`, `lldash`):

```
/rtp/<addr>:<port>/<fmt>    RTP source
/udp/<addr>:<port>/<fmt>    plain MPEG-TS over UDP source
/srt/<addr>:<port>/<fmt>    SRT source, caller mode, dedup'd by addr:port
```

These two are bare, with no address, and at most one of each can ever exist:

```
/rist/<fmt>     the -i rist://@host:port source
/stdin/<fmt>    the -i - source
```

These reach a channel list, indexed in `-i` definition order:

```
/list/<n>/item/<i>/<fmt>
/list/<n>/name/<channel>/<fmt>
```

A named `-i` (via `-n`, see above) is reachable the same way, `<name>` standing in for the whole
bare or `/list/<n>/` piece - both forms keep working side by side:

```
/<name>/<fmt>                    -i - or -i rist://@host:port
/<name>/item/<i>/<fmt>
/<name>/name/<channel>/<fmt>
```

`<fmt>` can be omitted (with or without a trailing slash); it then defaults to `ts`.

Any path above accepts `?filter=<pids>` to drop PIDs from the output (comma-separated decimal or
`0x`-hex, e.g. `?filter=101,0x20`). Disable this feature with `--no-pid-filters`.

On a multi-program transport stream (MPTS), `ts` always passes every program through unchanged.
`spts`/`hls`/`hls-fmp4`/`llhls`/`dash`/`lldash` demux a single program and default to the first PMT that
resolves. 
You can override this by selecting a specific PMT:`?pmt=<pid>` (decimal or `0x`-hex). 
`spts` is a continuous raw HTTP/TS like `ts`, just guaranteed single-program: only the locked program's PAT/PMT/PCR/ES PIDs go out.

`rawaudio` locks the same way (`?pmt=`, default first PMT that resolves), then picks that program's lowest-numbered audio ES 
that is not dropped by `?filter=` and forwards its PES payload as-is: raw data.


## Playlist export

The "List Export" page (or `/export/<fmt>/<type>` directly) lists every configured channel as
one playlist, for VLC/Kodi/tvheadend/... to subscribe to instead of hand-feeding one URL per channel.

```
/export/<fmt>/<type>
```

`<fmt>` is any of the entry formats above (`ts`, `spts`, `rawaudio`, `mp4`, `hls`, `hls-fmp4`, `llhls`,
`dash`, `lldash`); `<type>` is `m3u` or `xspf`.

Optional query params:
* `?host=<hostname_or_ip>[:port]`: base host for generated URLs. Default: the request's own
  `Host:` header, port from `--listen`/`--listen-tls` depending on HTTP/HTTPS.
* `?input=1,3,4`: include only these `-i` indices. Default: all.
* `?filter=<pids>`: forwarded onto generated URLs, if applicable.
* `?keep_multicast`: a channel whose source is `rtp://`/`udp://` gets listed by its raw
  multicast URI instead of a dipixy play-path.
* `?plain`: force `Content-Type: text/plain`.

A channel's `tvg-logo`/`<image>` (M3U/XSPF) carries through if the source playlist had one.

## Codecs

`ts`/`spts` and plain `hls` (TS segments) are a straight TS remux: any video/audio codec passes through untouched.
Segmenting still needs to locate keyframes, understood for MPEG-2 Video, H.264/AVC, H.265/HEVC, and H.266/VVC.
Other video codecs won't cut cleanly on an IDR/I-frame.

`mp4`, `hls-fmp4`, `llhls`, `dash` and `lldash` build actual ISOBMFF (fMP4) sample entries, so their codec support is narrower:
* video: H.264/AVC, H.265/HEVC, H.266/VVC. MPEG-2 Video has no fMP4 sample entry and won't produce output.
* audio: AAC (ADTS or LATM), AC-3, Enhanced AC-3 (E-AC-3), MPEG-1 Layer II (MP2), Opus.

`rawaudio` just forwards a program's lowest-numbered audio ES's PES payload as-is, codec-agnostic.

## DLNA / UPnP-AV

`dipixy` comes with an integrated DLNA server, disabled by default.

If enabled (`--enable-dlna`), dipixy announces itself as a UPnP MediaServer and serves a browsable ContentDirectory tree. 
You can override the accounced hostname using `--dlna-host`. If your network needs extra hops, `--ssdp-ttl` lets you customize its reach.

> Assign a `-n <name>` to your inputs if you want nicer list names in your UPnP tree.

If an input was marked as `--media-type radio`, the stream(s) affected get flagged as `upnp:class = object.item.audioItem.audioBroadcast` and served
as "/rawaudio". "tv" is always "/spts". 

`--dlna-name` lets you set a nicer name to appear on your TV (like "MyProvider TV").

M3U's `tvg-logo` and XSPF's `<image>`, as long as they point to a webserver, will get used as channel logos / picons if present.

## TLS

If `--tls-cert`/`--tls-key` aren't given, dipixy looks for `server.crt`/`server.key` in the
current working directory, in `/etc/dvbipitools/` and in `/etc/dvbipitools/dipixy/`.
If none are found, `--listen-tls` is simply left unbound, and that is not an error. 
`SIGUSR1` reloads the certificate without a restart.

## Dependencies

dipixy always speaks HTTP/1.1. 
HTTP/2 (`libnghttp2`) and HTTP/3 (`libngtcp2` + `libngtcp2_crypto_ossl` + `libnghttp3`) are each auto-detected at build time, and TLS itself
needs OpenSSL. 
Missing any of these disables that feature rather than failing the build. 

RIST and SRT input need `librist`/`libsrt` the same as every other tool here.

## Signals

* `^C`, SIGINT or SIGTERM: stop
* SIGHUP: re-read every `sds://`/playlist source and swap in the result; a source that fails to
  reload keeps serving its last-known-good list rather than going empty
* SIGUSR1: reload the TLS certificate

## Limitations

* Opening any form of HLS or DASH streams on a "cold" ingress stream means that there are no pre-produced segments available.
  This means that playback cannot start immediately.
* RIST and SRT don't support bonding in this tool. You can use `dipirist` or `dipisrt` with `-i -` as a work-around.
* Radio. DVB-IPI installations wildly differ on how they implemented this. Some deliver an SPTS with one Audio PES.
  Others go full OTT, deliver raw streams or pack them into an MPTS until it's the size of their smallest TV profile.
  Similar for client support. VLC or Kodi will happily play a DLNA radio playlist, Samsung or LG most probably won't -
  or maybe they do.
* No transcoding here. While burts won't be a problem, your DVB-IPI provider dislikes them as much as you do, heavy use
  of B-frames or a long GoP will collide with short segment sizes in HLS or DASH.
* M3U and XSPF input playlists can contain `rtp://`, `udp://`, `srt://` or HTTP(S)/TS, not RIST, HLS or DASH.

## Running under systemd

For a systemd-managed deployment, the unit below is a reasonable starting point. If
`--metrics-id` is set, dipixy also needs write access to the metrics socket, same as any other
reporter, see [dipimetrics](../dipimetrics/README.md#running-under-systemd).

```ini
[Unit]
Description=dipixy
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/dipixy -l 0.0.0.0:9080 -L 0.0.0.0:9443 -i /etc/dvbipitools/channels.m3u -n MyChannels
Restart=on-failure
RestartSec=5
StartLimitIntervalSec=60
StartLimitBurst=10
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadOnlyPaths=/etc/dvbipitools

[Install]
WantedBy=multi-user.target
```

## ETSI TS 102 905 (DVB-HN) "persona"

`dipixy` is not a DVB-HN compliant service by default, but provides command-line options to make it cover some parts of it.

* `--enable-dlna --dlna-host 10.0.0.2:9080' --ssdp-iface eth0` (IP address and network interface are examples) 
  will enable DLNA/UPnP-AV and using an SD&S input like `-i sds://239.19.75.1:3937 -n 'My Provider\'s IPTV` lets you watch its IPTV channels.
* By default, DLNA DIDLs contain SPTS over HTTP for all video content and raw audio over HTTP for radio. Clause 9.2 demands this
  to be the multicast address itself. If your devices and your home network topology support it, you can force this behavior
  by adding `--dlna-keep-multicast`. This will not affect inputs that were not multicasts in the first place - and as the
  streams themselves do not pass this service itself, no filters or PMT selection can be applied.

Known gaps (incomplete):
* It is _not_ a DLNA MediaRenderer (no AVTransport/RenderingControl), since it only relays streams and does not handle CoD/VoD.
* There is no BCG/EPG handling here.
* FCC/RET can only be supported if the respective input definition includes an FCC/RET service (like `dipifccret`).
  This can be the case for SD&S and TVA XML.

## udpxy "persona"

While `http://<address>:<port>/<cmd>/[src_address@]<mgroup_address><sep><mgroup_port>/` still works like at udpxy,
a few things were added:
* Protocols: IPv6, HTTP/2, HTTP/3, HTTPS, SSDP
* Ingress: MPTS demux, RIST, SRT, stdin
* Egress: HLS (TS and fMP4), Low-Latency HLS, MPEG-DASH (regular and CMAF Low-Latency), audio raw demux
* Operational: OpenMetrics
* Convenience:
  + "URL Builder"
  + PID filters
  + Playlists: M3U, XSPF, CSV, TVA XML, SD&S client (see `dipiscan` and `dipisds`)
  + DLNA / UPnP-AV

What behaves differently:
* "rtp" vs "udp" [cmd] is strict and not just about probe skipping

What's lost:
* udpxrec: `dipirec` can serve a similar purpose
* It's _not_ lightweight, especially not when compared to udpxy.
* No other chars than ":" to separate <mcast_group>:<port>
* Its environment vars


## Examples

```sh
# direct routes, no channel list needed
dipixy -l 0.0.0.0:9080
vlc http://localhost:9080/udp/239.1.1.1:5000/ts      # Progressive HTTP/TS
vlc http://localhost:9080/udp/239.1.1.1:5000/mp4     # Progressive HTTP/MP4
vlc http://localhost:9080/rtp/239.1.1.1:5000/dash    # MPEG DASH
vlc http://localhost:9080/srt/1.2.3.4:9000/hls       # HLS (TS segments)
vlc http://localhost:9080/srt/1.2.3.4:9000/hls-fmp4  # HLS (fMP4 segments)
vlc http://localhost:9080/srt/1.2.3.4:9000/llhls     # Low-Latency HLS

# live SD&S discovery as list 1
dipixy -i sds://239.19.75.1:3937

# a static M3U channel list, custom port
dipixy -l 0.0.0.0:9080 -i channels.m3u

# HTTPS on IPv6, 2 XSPF lists
dipixy -L [::]:9443 --tls-cert server.crt --tls-key server.key -i tv.xspf -i radio.xspf

# stdin, fed by ffmpeg
ffmpeg -re -i source.mkv -c copy -f mpegts - | dipixy -i - -l 0.0.0.0:9080
curl http://localhost:9080/stdin/hls-fmp4

# a RIST listener plus a pulled HTTP(S) source, indexed as list 1
dipixy -i rist://@0.0.0.0:9000 -i https://origin.example/live.ts
vlc http://localhost:9080/rist/dash
vlc http://localhost:9080/list/1/item/1/llhls

# LL-HLS, PID-filtered, one program picked out of an MPTS (by the PID of its PMT)
vlc "http://localhost:9080/udp/239.1.1.1:5000/llhls?filter=0x100,0x101&pmt=0x1000"

# LL-DASH
dipixy -l 0.0.0.0:9080
vlc http://localhost:9080/udp/239.1.1.1:5000/lldash

# guaranteed single-program TS out of an MPTS, same PMT selection as above
vlc "http://localhost:9080/udp/239.1.1.1:5000/spts?pmt=0x1000"

# metrics alongside dipimetrics
dipixy -l 0.0.0.0:9080 --metrics-id xy1

# named inputs, addressed by name instead of index
dipixy -i tv.xspf -n tv -i rist://@0.0.0.0:9000 -n backup
vlc http://localhost:9080/tv/item/1/hls
vlc http://localhost:9080/backup/ts

# DLNA MediaServer, browsable by any UPnP renderer on the LAN
dipixy -l 0.0.0.0:9080 -i channels.m3u --enable-dlna --dlna-host 10.0.0.2:9080
```

For a homelab setup, combining it with some `dipiscan` cronjob, letting it update playlists and
issue a `SIGHUP` to a running `dipixy` with DLNA enabled may enable a "box-less" IPTV experience.
