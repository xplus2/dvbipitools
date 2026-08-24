# dipidescramble

Standalone counterpart to `dipitvhead`'s scrambling: 
It reads a (possibly scrambled) transport stream, extracts CAT/PMT, 
pulls ECM/EMM off their PIDs (or once at startup via unicast interface),
matches if it's ours (`-s`), decrypts the RSA -> BK -> SK -> CW chain with the device's private key
and descrambles the stream in place, writing plain `.ts`/`.mkv`/`.mka`, or pushing it live to an 
RTMP(S) ingest server. 

This tool is mainly meant for automated tests.
Since it uses/needs the device's private key, end-users can hardly ever provide what's needed.

To speed up tests and/or to simulate an already provisioned client, the EMM cache
can be preloaded with the decrypted EMM-U/EMM-G sections from a previous run (`--emm-file`).

The CAS scheme is auto-detected from the stream itself (PMT `CA_descriptor`/`scrambling_descriptor`).
`-k`/`-s`/`-e` or `--biss-*` are only required once the stream turns out to need them.

## Options

| flag  | long form               | argument              | default                                             |
|-------|-------------------------|-----------------------|-----------------------------------------------------|
| `-i`  | `--input`               | `<uri>`               | required                                            |
| `-k`  | `--key`                 | `<path>`              | required for ECM/EMM-driven CAS                     |
| `-s`  | `--serial`              | `<id>`                | required for ECM/EMM-driven CAS                     |
| `-e`  | `--emm-file`            | `<path>`              | required for ECM/EMM-driven CAS (cache file)        |
| `-u`  | `--unicast-emm`         | `<uri>`               |                                                     |
|       | `--insecure`            |                       | off (`-u`, or `-o rtmps://`)                        |
| `-o`  | `--output`              | `<target>`            | required, repeatable                                |
| `-f`  | `--format`              | `ts\|mkv\|mka`        | `ts`                                                |
| `-p`  | `--pmt-pid`             | `<pid>` / `all`       | none (see below)                                    |
| `-I`  | `--iface`               | `<iface>`             | kernel default                                      |
|       | `--ecm-profile`         | `<spec>`              | `ecm_profile` templating, see below                 |
| `-v`  | `--verbose`             |                       | off                                                 |
|       | `--color`               | `auto\|always\|never` | `auto`                                              |
|       | `--metrics`             | `<path>`              | `/run/dvbipitools/metrics.sock`                     |
|       | `--metrics-id`          | `<name>`              | none (metrics disabled unless set)                  |
|       | `--metrics-interval`    | `<s>`                 | `5`                                                 |
|       | `--max-services`        | `<n>`                 | `32` (max `256`)                                    |
|       | `--profile`             | `simple\|main`        | `simple`, `-i rist://` only                         |
| `-d`  | `--daemonize`           |                       | off (foreground)                                    |
| `-h`  | `--help`                |                       |                                                     |

### Related to BISS
| flag  | long form               | argument              | default                                             |
|-------|-------------------------|-----------------------|-----------------------------------------------------|
|       | `--biss1-sw`            | `<hex12>`             | BISS1 Mode 1, mutually exclusive with `--biss2-*`   |
|       | `--biss2-sw`            | `<hex32>`             | BISS2 Mode 1, mutually exclusive with `--biss2-esw` |
|       | `--biss2-esw`           | `<hex32>`             | BISS2 Mode E                                        |
|       | `--biss2-id`            | `<hex32>`             | required with `--biss2-esw`                         |
|       | `--biss2-ca-key`        | `<path>`              | BISS2 Mode CA: receiver RSA private key, PEM        |

### Related to SRT Inputs/Outputs
| flag  | long form               | argument              | default                                             |
|-------|-------------------------|-----------------------|-----------------------------------------------------|
|       | `--srt-passphrase-in`   | `<pw>`                | none, `-i srt://` only, 10..79 chars                |
|       | `--srt-pbkeylen-in`     | `16\|24\|32`          | `16`, requires `--srt-passphrase-in`                |
|       | `--srt-streamid-in`     | `<id>`                | none, `-i srt://` only                              |
|       | `--srt-packetfilter-in` | `<cfg>`               | none, `-i srt://` only                              |
|       | `--srt-latency-in`      | `<ms>`                | library default, `-i srt://` only                   |
|       | `--srt-passphrase`      | `<pw>`                | none, every `-o srt://` target, 10..79 chars        |
|       | `--srt-pbkeylen`        | `16\|24\|32`          | `16`, requires `--srt-passphrase`                   |
|       | `--srt-streamid`        | `<id>`                | none, every `-o srt://` target                      |
|       | `--srt-packetfilter`    | `<cfg>`               | none, every `-o srt://` target                      |
|       | `--srt-latency`         | `<ms>`                | library default, every `-o srt://` target           |

---

## Parameters

### Input (`-i`)

Can be a single-program stream (SPTS) or a multi-program mux (MPTS), see `-p` below.

| schema                          | what's this?                                                      |
|---------------------------------|-------------------------------------------------------------------|
| `rtp://@<group>:<port>`         | RTP wrapped SPTS or MPTS multicast                                |
| `udp://@<group>:<port>`         | plain SPTS or MPTS multicast                                      |
| `-`                             | stdin, already-demuxed `.ts` (e.g. piped from `dipirec`/`ffmpeg`) |
| `rist://@<host>:<port>[?query]` | single-peer RIST receiver, requires librist                       |
| `srt://<host>:<port>`           | single-peer SRT, calls out, requires libsrt                       |
| `srt://@<host>:<port>`          | single-peer SRT, listens, requires libsrt                         |

`rist://@host:port[?query]` is also accepted. It requires librist and only supports a single peer per input (no bonding).
If you need bonded RIST input, you can run `dipirist` in front of this tool as a bridge instead.
Encrypted input needs `--profile main` and a `?secret=` query parameter in the URI.

`srt://host:port` (caller) or `srt://@host:port` (listener) is also accepted. It requires libsrt
and only supports a single peer (no bonding, no rendezvous). You can use `dipisrt` as a bridge for those.
Encryption/streamid/latency/packet filtering are set via `--srt-passphrase-in`/`--srt-pbkeylen-in`/
`--srt-streamid-in`/`--srt-packetfilter-in`/`--srt-latency-in`.


### MPTS input (`-p`)

On connect, the tool waits for the PAT and checks how many programs the source carries.
The CW derived from the ECM/EMM chain applies mux-wide either way, so descrambling itself needs nothing extra.
`-p` only decides what gets selected/labeled in the output:

* SPTS: `-p` is ignored, but issues a warning.
* MPTS: 
  * `-p <pid>`: pins that one program. Doesn't change `-f ts` bytes (see below), but drives which
    program's tracks a `mkv`/`mka` output builds. Rejected if the pid isn't in the PAT.
  * `-p all`: descramble the whole mux.
    * `-f ts`: already whole-mux by default - every packet is decrypted and forwarded regardless of program,
      so `-p all` changes nothing about the bytes written, just skips the fail-early check below.
    * `-f mka`: every program's audio becomes its own track, each labeled with that program's own SDT name once it arrives.
    * `-f mkv`: rejected - pick one program with `-p <pid>` instead (Matroska has no video/audio track
      association across several programs).
  * neither given: fails early, after a brief wait for each program's SDT name, listing the available `sid`/PMT pid/name.
    A program whose name never arrives in that window shows as `(no SDT)` rather than blocking further.

### Device key (`-k`)

PEM-encoded RSA private key (`EK`) for a single device, as issued by the CAS on
device creation. Decryption of EMM-U/EMM-G/ECM all happens locally with this key.

### Device serial (`-s`)

Matched against EMM-U addressing. EMM-U for any other device on the same carousel is skipped, no decrypt attempted.

### EMM cache (`-e`)

File where decrypted EMM-U/EMM-G sections are kept, so a restart doesn't need to wait for the broadcast
EMM cadence (or `-u`/`--unicast-emm`) again before it can descramble.
Loaded on startup if present, rewritten whenever the cached state changes.

### Unicast EMM pull (`-u`, `--unicast-emm`)

Optional. If your CAS or TV platform supports pulling EMMs directly, startup time with an unpopulated EMM cache 
is greatly reduced as this tool does not have to wait for its serial to show up in the carousel.

```
-u https://<token>@<host>:<port>/device/<serial>/emm
```

`--insecure` skips TLS verification for this connection (self-signed, hostname,
expiry) - useful against a test instance.

Fetched once at startup only, not polled.

### Output (`-o`)

Repeatable: e.g. a file plus one or more RTMP(S) pushes.

| schema                               | what's this?                          |
|--------------------------------------|---------------------------------------|
| `<path>`                             | a file                                |
| `-`                                  | stdout                                |
| `rtmp://<host>[:port]/<app>/<key>`   | RTMP publish, default port `1935`     |
| `rtmps://<host>[:port]/<app>/<key>`  | RTMP over TLS, default port `443`     |
| `srt://host:port`                    | SRT push, calls out (requires libsrt) |

RTMP output ignores `-f`: descrambled H.264/HEVC video. Unsupported video (MPEG-2) or audio
(MP2) is dropped from that push.

`srt://` output always calls out, since `srtout` has no listener mode. Each `-o srt://` target
is independent and not bonded. Repeat `-o` for more targets, or use `dipisrt` if you need bonded
peers. Like RTMP, it ignores `-f` and always sends the raw descrambled TS. Encryption, streamid,
latency, and packet filtering apply to every `-o srt://` target, via `--srt-passphrase`,
`--srt-pbkeylen`, `--srt-streamid`, `--srt-packetfilter`, and `--srt-latency`.

`-f mkv`/`mka` can combine with an `rtmp(s)://`/`srt://` target, given exactly one plain file target for
the Matroska mux itself. `-p all` can't, same reason as `-f mkv`: RTMP/SRT here carry one program.

`--insecure` (also `-u`, above) skips cert/hostname/expiry checks, for `rtmps://`.

A push target reconnects on its own on a drop, other `-o` targets keep going regardless. After a
(re)connect it waits for the next keyframe, same as any live encoder joining mid-GOP.

### Output format (`-f`)

`ts` (default): raw descrambled transport stream, packet for packet.

`mkv`/`mka`: demuxes the descrambled stream and writes a Matroska container instead - useful for eyeballing/playing back a test capture
directly rather than feeding the raw `.ts` through a separate remux step. All audio tracks are muxed, no subtitle output.

This only applies to plain file `-o` targets; an `rtmp(s)://` target always gets FLV, see above.

### BISS (`--biss1-sw`, `--biss2-sw`, `--biss2-esw`/`--biss2-id`)

BISS Mode 1/E: both BISS1 and BISS2 (EBU Tech 3292) share `ca_system_id 0x2602`, is detected from the PMT's `CA_descriptor`.
The *value length* the operator supplies picks the cipher, since both share the same signaling:

* `--biss1-sw <hex12>`: 12 hex chars, BISS1 Mode 1, CSA1
* `--biss2-sw <hex32>`: 32 hex chars, BISS2 Mode 1 or `--biss2-esw <hex32> --biss2-id <hex32>` for BISS2 Mode E, CISSA.

### BISS-CA (`--biss2-ca-key`)

BISS2 Mode CA (EBU Tech 3292-s1, `ca_system_id 0x2610`), detected from the PMT's `CA_descriptor`.

`--biss2-ca-key <path>`: this receiver's RSA private key, PEM. The `entitlement_key_id` is derived from
it and matched against the stream's EMM; the Session Key it decrypts then decrypts the ECM's Session
Word(s), same as `-k` does for the generic ECM/EMM CAS path but with BISS-CA's own RSA-OAEP/AES-CBC/CISSA
key hierarchy, not that CAS's.

### ECM profile (`--ecm-profile`)

Define a flexible template to match the ECM formatting of a CAS you want to test/integrate/develop/debug.

While the DVB-SimulCrypt standard defines the transport, it leaves the internal formatting of an ECM undefined.
Since `dipitvhead` and `dipiradiohead` treat these payloads as transparent blobs, 
this profile provides the parsing logic necessary for a consistent lab setup and automated verification.
Since hardcoding defaults would defeat the purpose of integration and the specifics needed for handling these payloads correctly are out of scope here,
the following mechanics allow the configuration to be driven by the most authoritative source: you.


`<spec>` is comma-separated `key=value`; `+` separates ordered-list values,
`:` separates a header id from its hex bytes, `header=` repeats (up to 4):

```
--ecm-profile cipher=<c>,iv=<src>,padding=<p>,
  hkdf=<0|1>,enc_info=<s>,mac_info=<s>,short_key_source=<truncate|separate_info>,short_key_info=<s>,
  header=<id>:<hex>[,header=<id>:<hex> ...],
  include_cp_number=<0|1>,include_ecm_id=<0|1>,
  field_order=<tok+tok+...>,wire_order=<tok+tok+...>,
  cp_number_layout=<back|front>,
  integrity=<none|crc32|hmac-sha256>,integrity_order=<after-encrypt|before-encrypt>,
  truncate_tag=<4|8>,truncate_from=<left|right>,
  bind_ecm_id=<0|1>,bind_cp_number=<0|1>,crc32_variant=<ieee|castagnoli>,crc32_endian=<big|little>,
  cw_count=<n>,cw_group=<tok+tok>,
  ecm_id=<val>
```

`cipher`: `aes128-ecb|aes256-ecb|aes128-cbc|aes256-cbc|aes128-gcm|aes256-gcm|des-ede3-ecb|des-ede3-cbc|des-ede-ecb|des-ede-cbc`
default=`aes256-ecb`. 
Inclusion of DES is for preservational and educational reasons only. You are encouraged not to pull an _Isla Nublar_ by
unleashing a dinosaur in modern day.

Tokens (`field_order`/`wire_order`/`cw_group`): the fixed keywords
`ecm_id`/`cp_number`/`cw`/`cw_group`/`integrity_tag`/`iv`/`ciphertext`/`gcm_tag`, plus any declared
header id. 

`ecm_id` isn't on the wire and isn't derivable from the transport stream by a standardized receiver,
so it's an explicit configuration parameter. Unset, it falls back to the stream's own ECM PID whenever `include_ecm_id`/`bind_ecm_id` need a value.

Under `cw_count > 1` (lead-CW packing), every combo in the ciphertext still gets fully decrypted and 
checked for integrity, but only the first (current) one is applied. There is no pre-fetch of the lead/next combo(s).


## Examples

```sh
dipidescramble -i rtp://@239.0.0.1:1975 -k device.key -s mysmartcardserial-01 -e emm.cache -o out.ts -v
dipidescramble -i rtp://@239.0.0.1:1975 -k device.key -s mysmartcardserial-01 -e emm.cache -o out.mkv -f mkv -v

# MPTS source: descramble the whole mux, one audio track per program
dipidescramble -i rtp://@239.0.0.1:1975 -k device.key -s mysmartcardserial-01 -e emm.cache -o out.mka -f mka -p all

# legacy BISS1 Mode 1, no device key needed
dipidescramble -i rtp://@239.0.0.1:1975 --biss1-sw 0123456789ab -o out.ts -v

# BISS2 Mode 1, no device key needed
dipidescramble -i rtp://@239.0.0.1:1975 --biss2-sw 00112233445566778899aabbccddeeff -o out.ts -v

# BISS2 Mode CA: this receiver's own RSA private key
dipidescramble -i rtp://@239.0.0.1:1975 --biss2-ca-key receiver1.key -o out.ts -v

# descramble and push live, keeping a local copy too
dipidescramble -i rtp://@239.0.0.1:1975 --biss1-sw 0123456789ab -o out.ts -o rtmp://live.example.com/app/key

# complex ECM profile
dipidescramble -i rtp://@239.0.0.1:1975 -k device.key -s mysmartcardserial-01 -e emm.cache -o out.ts \
  --ecm-profile cipher=aes128-cbc,iv=cp_number,padding=pkcs7,header=h1:AABB,include_cp_number=1,include_ecm_id=1,integrity=hmac-sha256,truncate_tag=8
```

## Notes

* ECM/EMM-driven CAS descrambling assumes `AES-256-ECB` for ECMs by default (mathematically
  identical to `AES-256-CBC` with an empty IV), unless `--ecm-profile` overrides it.
