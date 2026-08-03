# dipidescramble

Standalone counterpart to `dipitvhead`'s CAS scrambling: 
reads a (possibly scrambled) transport stream, extracts CAT/PMT, 
pulls ECM/EMM off their PIDs (or once at startup via unicast interface),
matches if it's ours (`-s`), decrypts the RSA -> BK -> SK -> CW chain with the device's private key
and descrambles the stream in place, writing plain `.ts` or `.mkv/mka`.

This tool is mainly meant for automated tests.
Since it uses/needs the device's private key, end-users can hardly ever provide what's needed.

To speed up tests and/or to simulate an already provisioned client, the EMM cache
can be preloaded with the decrypted EMM-U/EMM-G sections from a previous run.

## Options

| flag  | long form       | argument              | default        |
|-------|-----------------|-----------------------|----------------|
| `-i`  | `--input`       | `<uri>`               | required       |
| `-k`  | `--key`         | `<path>`              | required       |
| `-s`  | `--serial`      | `<id>`                | required       |
| `-e`  | `--emm-file`    | `<path>`              | required       |
| `-u`  | `--unicast-emm` | `<uri>`               |                |
|       | `--insecure`    |                       | off            |
| `-o`  | `--output`      | `<path\|->`           | required       |
| `-f`  | `--format`      | `ts\|mkv\|mka`        | `ts`           |
| `-I`  | `--iface`       | `<iface>`             | kernel default |
| `-v`  | `--verbose`     |                       | off            |
|       | `--color`       | `auto\|always\|never` | `auto`         |
| `-h`  | `--help`        |                       |                |

## Parameters

### Input (`-i`)

`udp://`/`rtp://` multicast, or `-` for stdin (already-demuxed `.ts` on stdin, e.g. piped from `dipirec`/`ffmpeg`).

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

Descrambled output, file or `-` for stdout.

### Output format (`-f`)

`ts` (default): raw descrambled transport stream, packet for packet.

`mkv`/`mka`: demuxes the descrambled stream and writes a Matroska container instead - useful for eyeballing/playing back a test capture
directly rather than feeding the raw `.ts` through a separate remux step. All audio tracks are muxed, no subtitle output.

## Examples

```sh
dipidescramble -i rtp://@239.0.0.1:1975 -k device.key -s mysmartcardserial-01 -e emm.cache -o out.ts -v
dipidescramble -i rtp://@239.0.0.1:1975 -k device.key -s mysmartcardserial-01 -e emm.cache -o out.mkv -f mkv -v
```
