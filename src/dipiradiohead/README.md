# dipiradiohead

Fetches one or more Icecast/Shoutcast streams and re-muxes them as one DVB-IPI multicast. No
transcoding. A single `-i` gives a normal SPTS; more than one gives an MPTS, one program per input.

```
dipiradiohead -i <uri> [--sid <n>] [--sdt <name>] [-i <uri> ...] {-m <mcast>:<port>|-R <uri>} [options]
```

## Options

| flag  | long form            | argument                               | default              | scope      |
|-------|----------------------|----------------------------------------|----------------------|------------|
| `-i`  | `--input`            | `<uri>`                                | required, repeatable |            |
|       | `--sid`              | `<n>`                                  | auto (see below)     | per-input  |
| `-s`  | `--sdt`              | `<name>`                               | auto (see below)     | per-input  |
| `-m`  | `--mcast`            | `<group>:<port>` / `[<group6>]:<port>` | required unless `-R` given |    |
| `-I`  | `--iface`            | `<iface>`                              | kernel route         |            |
| `-r`  | `--rtp`              |                                        | off (plain UDP)      |            |
| `-T`  | `--ttl`              | `<n>`                                  | 1 (kernel default)   |            |
| `-R`  | `--rist`             | `rist://host:port`                     | none, repeatable (bonded) |       |
|       | `--profile`          | `simple\|main`                         | `simple` (`-R` peers only) |      |
|       | `--secret`           | `<psk>`                                | none (`-R` peers only) |          |
|       | `--cname`            | `<name>`                               | library default (`-R` peers only) | |
|       | `--buffer`           | `<ms>`                                 | library default (`-R` peers only) | |
| `-n`  | `--nit`              | `<text>`                               | none                 |            |
| `-e`  | `--error`            | `<seconds>`                            | see below            |            |
| `-k`  | `--insecure`         |                                        | off (TLS verified)   |            |
|       | `--tsid`             | `<n>`                                  | 1                    |            |
|       | `--onid`             | `<n>`                                  | 1                    |            |
| `-v`  | `--verbose`          |                                        | off                  |            |
|       | `--color`            | `auto\|always\|never`                  | `auto`               |            |
|       | `--metrics`          | `<path>`                               | `/run/dvbipitools/metrics.sock` |   |
|       | `--metrics-id`       | `<name>`                               | none (metrics disabled unless set) | |
|       | `--metrics-interval` | `<s>`                                  | `5`                  |            |
| `-h`  | `--help`             |                                        |                      |            |

`--sid`/`-s` pair with whichever `-i` came right before them - like ffmpeg's per-input options,
not global flags. Order matters: `--sid`/`-s` before the first `-i` is an error.

### Conditional Access: SimulCrypt

`--cas-ecmg` is repeatable, one CAS vendor per `--cas-ecmg`. Flags marked **per-vendor** pair
with the `--cas-ecmg` immediately before them, same convention as `-i`'s `--sid`/`-s` above -
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

## Multiple inputs (MPTS)

Repeat `-i` for more stations; each becomes its own program in one output MPTS. Every input is
opened and retried fully independently - one station being down never affects, delays or stops
any other, and never stops the output itself. PAT always lists every configured program,
including ones currently down; a down program just stops contributing PMT/audio/SDT/EIT until
it reconnects, everything else keeps going (including CAS, see below). If every input is down
at once, the output keeps running with PSI tables only, no audio, rather than exiting.

PIDs are assigned by input order, not configurable: PMT `0x1000 + i`, audio `0x0100 + i` for the
i-th `-i` (0-based) - matching the ffmpeg mpegts muxer's own default PID bases (`pmt_start_pid`/
`start_pid`), so an MPTS from dipiradiohead lines up with what other common tooling would produce.
`--sid`/`-s` per input as above; if omitted, `--sid` auto-assigns the lowest free number and
`--sdt` defaults to `dipiradiohead <n>` (1-based) so stations aren't silently given identical
names.

A single `-i` is unaffected by any of this: same fixed PMT `0x0100`/audio `0x0101` as before,
same `-s` default of plain `dipiradiohead`.

> Note: Multi Program Transport Streams rely on your local clock reference.
> It is not something you would usually run on a Raspberry Pi.

## Output (`-m`, `-I`, `-r`, `-R`)

`-m <group>:<port>` / `-m [<group6>]:<port>`. `-I` sets the outgoing interface (default: kernel
route). `-r` wraps output in RTP, matching `dipirec -i rtp://`; without it, plain UDP, matching
`-i udp://`. 7 TS packets (1316 B) per datagram either way. `-T` sets the multicast TTL / hop
limit (default 1, i.e. link-local only - raise it to route beyond the first hop).

`-R rist://host:port[?query]` sends the same TS to one or more RIST peers, alongside `-m` if
given, or standalone without it. One of `-m`/`-R` is required, not both (requires librist). 
Repeatable, every peer bonds onto a single RIST sender context, 
same bonding model as [dipirist](../dipirist/README.md). `-r` only affects the
`-m` output.
RIST is never RTP-wrapped. 
`--profile`/`--secret`/`--cname`/`--buffer` configure the RIST peers.
`--secret` requires `--profile main`.

## Now-playing metadata

EIT present event (table 0x4E, no following), text = `[artist] [title]` / `[artist]` / `[title]`.

Source, auto-detected per stream:
* `icy-metaint` header present -> ICY: `StreamTitle='...'` blocks parsed + stripped at that byte
  interval. Splits on first `" - "` into artist/title; no split found -> title only.
* no `icy-metaint` -> inline ID3v2: `ID3` sync checked only at audio frame boundaries (never
  mid-frame). `TIT2`/`TPE1` (v2.3+v2.4, text or UTF-16) read, tag stripped from the ES.

Repetition: PAT/PMT 100ms, SDT 2s, NIT 10s (only if `-n` set), EIT 1s or immediately on change -
per program with more than one `-i`, all sharing one PAT/NIT and one continuity counter per
table type (SDT/EIT sections cycle across programs on those shared PIDs, not duplicated per
program). Fixed PIDs (single `-i`): PAT 0x0000, NIT 0x0010, SDT 0x0011, EIT 0x0012, PMT 0x0100,
audio 0x0101. Per-program PIDs (multiple `-i`): see "Multiple inputs" above.

## Service info (`-n`, `-s`)

`-n` NIT `network_name`, one for the whole output. `-s` SDT `service_name` per program (provider
name is fixed: `dipiradiohead`). UTF-8.

## Identifiers (`--tsid`, `--onid`, `--sid`)

transport_stream_id / original_network_id, default 1, one for the whole output. `--sid`
(service_id/program_number) is per input - see "Multiple inputs" above.

## Reconnecting (`-e`)

Single `-i`: no `-e` means any fetch error stops the tool; `-e <seconds>` reopens the input after
the delay, with multicast socket, continuity counters and PSI versions staying up across the gap.

Multiple `-i`: each input always retries independently (the tool can't just stop because the
whole point of several inputs is that the output keeps going) - `-e <seconds>` sets the interval
for all of them; if omitted, a 5s default is used instead of failing.

## Live stats (`-v`)

One self-updating line on stderr, about once a second.

## CAS SCS and Scrambler (`--cas-*`)

Acts as a DVB Simulcrypt SCS towards one or more ECMGs (one per `--cas-ecmg`), and as the
EMMG-side MUX towards each one's EMMG client, per ETSI TS 103 197 (protocol versions 2 and 3,
auto-negotiated per vendor unless that vendor's `--cas-ecmg-version` / `--cas-emmg-version` pins one).
Scrambles CISSA (ETSI TS 103 127, 128-bit AES-CBC, needs OpenSSL) or CSA1+2 (need libdvbcsa) content
and emits one `CA_descriptor` per vendor plus one shared `scrambling_descriptor` in each program's PMT,
a CAT with one `CA_descriptor` per vendor, and each vendor's own ECM (`--cas-ecm-pid`) and EMM (`--cas-emm-pid`) streams. 

There's no `--cas-pids` here (like in `dipitvhead`): every configured audio PID is scrambled, always.
With a single `-i` that's the one audio PID; with several, every program's audio, all under one shared CW/crypto-period
for the whole output regardless of vendor count, not independent per-program crypto.

The one exception: with CSA1/2's SIMD batching, only the first `-i`'s
audio is guaranteed never delayed by a batch window. The others may see minimal bounded PCR jitter under CSA1/2.

> Note: Adding an EMM stream to a single radio channel will quickly add more overhead than the actual payload.
> With multiple `-i`, this is handled automatically - one EMM stream covers every program, not one
> per station. Further options if EMM overhead is still a concern:
> * If you have an IPTV platform, use unicast EMMs all the way.
> * If you have TV and radio sharing a bouquet, let the EMM carousel spin on TV channels only.
> * If you do not have many subscribers (like a contribution use-case), keep the EMMG bitrate low.
> * If you are broadcasting this (not IPTV), consider collecting multiple radio stations into a single MPTS output.

### Crypto-period clock

With a single `-i`, dipiradiohead builds the whole stream itself, so unlike dipitvhead it
doesn't need to observe a PCR or defend against a stray source clock: crypto-period cadence
tracks the tool's own sample-accurate audio clock directly, always present from the first frame.
With multiple `-i`, there's no single audio clock to share across independent programs, so the
crypto-period instead tracks wall-clock time directly - unaffected by any one program
connecting, dropping or reconnecting.

### On ECMG loss (`--cas-resilience`)

* `frozen` (default): keeps scrambling with the last known-good CW indefinitely until the ECMG
comes back. The crypto period stays put on whatever it was, the service never blacks out.
* `cycling`: keeps flipping parity on the normal crypto-period schedule, alternating between
the last two known CWs (even/odd), instead of freezing on one.
* `silent`: stops sending the ECM PID once the ECMG becomes unreachable, instead of resending
the last known-good ECM. Content stays scrambled with the last known-good CW, same as `frozen` -
only the ECM stream itself goes quiet.

### EMMG (`--cas-emmg-port`, `--cas-emmg-version`)

dipiradiohead is the EMMG-side MUX: it listens (`--cas-emmg-port`, default 8002) and the EMMG
client connects to it, once per `--cas-ecmg` vendor (each with its own `--cas-emmg-port`), not
the reversed topology. Accepts whichever protocol version the client proposes unless that
vendor's `--cas-emmg-version` is set. EMM datagrams are queued and drained onto that vendor's
own `--cas-emm-pid` on arrival.

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
Mutually exclusive with `--biss2-sw`/`--cas-algo`/`--cas-ecmg`.
Same `CA_descriptor`/CAT signaling as BISS2 Mode 1/E, only the cipher (CSA1, not CISSA) differs. BISS1 Mode E (DES) is not supported.

`--biss2-sw <hex32>` scrambles with BISS2 Mode 1/E (EBU Tech 3292) instead: a static 32 hex  Session Word used directly
as the CISSA key, no ECMG/EMMG at all. Mutually exclusive with `--cas-algo`/`--cas-ecmg`.
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

Emits a real `CA_descriptor` (`CA_system_id 0x2610`, real ECM PID) with a
`bissca_entitlement_session_id_descriptor`, a real (non-empty) CAT, and a `scrambling_descriptor` (CISSA).
ECM/EMM PIDs are auto-allocated.

### Limitations

CISSA, CSA1, CSA2, BISS1 Mode 1, BISS2 Mode 1/E, BISS2 Mode CA only.

* No CSA3
* No BISS1 Mode E (DES)
* BISS2 Mode CA: no group key pairs, but one keypair per file in receivers-dir.
  No `entitlement_priv_data_loop` vendor extensions, `prevent_descrambled_forward`/
  `prevent_decoded_forward`/`insert_watermark` entitlement flags always 0 (unenforced)
* This is a single-pass scrambler


### Dependencies

CAS support is a CMake/configure-time option (`DIPIRADIOHEAD_CAS`, on by default).
* CISSA needs OpenSSL (same dependency as HTTPS input)
* CSA1, CSA2 and BISS1 dlopen libdvbcsa at runtime, if it is present. It is not built against it and does not bundle it.
* `-R`/RIST output needs librist (`DVBIPITOOLS_RIST` / `HAVE_RIST`, toolkit-wide).

Missing any of these degrades gracefully to "that feature unavailable", not a build failure.

Release builds and the packaged `.deb` in this repository do not contain libdvbcsa.
Install it on the machine running the tool if you want CSA1/CSA2/BISS1.

## Signals

* `^C`, SIGINT or SIGTERM: Stop
* SIGHUP: re-read BISS2-CA ceritficates directory (in case you added/removed/changed them)


## Running under systemd

For a systemd-managed deployment, the unit below is a reasonable starting point.
`-e` is included so a dropped input reopens itself instead of relying on a full process restart.

> Note: This service is a micro SCS/muxer/scrambler. How many of them you want to pack on a host is entirely yours.
> Make sure to do integration tests particularly addressing how the load resulting from this decision is handled.
> Replace `instancename` to make them easier to distinguish.

```ini
[Unit]
Description=dipiradiohead-instancename
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/dipiradiohead -i https://example.com/radiostation.m3u -e 5 -m 239.1.1.1:5000 -r -s "Best Hits Ever Radio"
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
dipiradiohead -i https://radio.example.com/channel1.m3u -m 239.1.1.1:5000 -r -s "Channel 1"
dipiradiohead -i http://radio.example.com/stream.aac -m 239.1.1.2:5000 -e 5
dipiradiohead -i http://radio.example.com/stream.mp3 -m 239.5.5.5:6000 & dipirec -i udp://@239.5.5.5:6000 -o kronehit.mka

# MPTS: two stations, one output, independently retried
dipiradiohead -i https://radio.example.com/channel1.m3u --sdt "Channel 1" \
              -i http://radio.example.org/channel2.aac --sdt "Channel 2" \
              -m 239.1.1.3:5000 -r -e 5

# scrambled (CISSA), ECMG at ecmg.example:2222
dipiradiohead -i https://radio.example.com/channel1.m3u -m 239.1.1.1:5000 -r -s "Channel 1" \
  --cas-algo cissa --cas-ecmg tcp://ecmg.example:2222 --cas-super-id 0x4A750002 --cas-ecm-id 1

# multi-CAS: two vendors, one required. --cas-ecmg opens a slot; the flags after it
# (version/super-id/ecm-id/pid/resilience/required) pair with that --cas-ecmg.
dipiradiohead -i https://radio.example.com/channel1.m3u -m 239.1.1.1:5000 -r -s "Channel 1" \
  --cas-algo cissa \
  --cas-ecmg tcp://ecmg-a.example:2222 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
             --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port 8002 --cas-required \
  --cas-ecmg tcp://ecmg-b.example:2222 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
             --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port 8003 --cas-resilience silent \
  --cas-fallback-clear

# legacy BISS1 Mode 1: 12 hex char Session Word, CSA1
dipiradiohead -i https://radio.example.com/channel1.m3u -m 239.1.1.1:5000 -r -s "Channel 1" \
  --biss1-sw 0123456789ab

# BISS2 Mode 1/E: no ECMG/EMMG needed
dipiradiohead -i https://radio.example.com/channel1.m3u -m 239.1.1.1:5000 -r -s "Channel 1" \
  --biss2-sw 00112233445566778899aabbccddeeff

# BISS2 Mode CA: per-receiver RSA entitlement, one pubkey per file under the dir
dipiradiohead -i https://radio.example.com/channel1.m3u -m 239.1.1.1:5000 -r -s "Channel 1" \
  --biss2-ca-receivers /etc/biss-ca/receivers
```
