# dipisrt

`dipisrt` bridges a DVB-IPI stream between a plain RTP/UDP/file endpoint and an SRT link, in
either direction. The direction is detected, depending on `-i`/`-o` being an `srt://` URI.

Dependencies:
* `libsrt`

Usage:
```
dipisrt -i <uri> -o <uri> [options]
```

## Options

| flag | long form            | argument              | default                             |
|------|----------------------|-----------------------|-------------------------------------|
| `-i` | `--in`               | `<uri>`               | required                            |
| `-o` | `--out`              | `<uri>`               | required                            |
| `-I` | `--iface`            | `<iface>`             | kernel route (non-SRT side only)    |
| `-k` | `--insecure`         |                       | off (`-i https://` source only)     |
|      | `--group-mode`       | `broadcast\|backup`   | none (required when bonding)        |
|      | `--rendezvous`       |                       | off (needs `--local`)               |
|      | `--local`            | `<host:port>`         | none (required with `--rendezvous`) |
|      | `--passphrase`       | `<pw>`                | none (10..79 chars)                 |
|      | `--pbkeylen`         | `16\|24\|32`          | `16` (only with `--passphrase`)     |
|      | `--streamid`         | `<id>`                | none                                |
|      | `--packetfilter`     | `<cfg>`               | none, e.g. `fec,cols:10,rows:5`     |
|      | `--latency`          | `<ms>`                | library default                     |
|      | `--send-buffer-mult` | `<n>` (1..32)         | `4` (sender side only)              |
|      | `--color`            | `auto\|always\|never` | `auto`                              |
|      | `--metrics`          | `<path>`              | `/run/dvbipitools/metrics.sock`     |
|      | `--metrics-id`       | `<name>`              | none (metrics disabled unless set)  |
|      | `--metrics-interval` | `<s>`                 | `5`                                 |
| `-v` | `--verbose`          |                       | off                                 |
| `-d` | `--daemonize`        |                       | off (foreground)                    |
| `-h` | `--help`             |                       |                                     |

## Endpoints (`-i`/`-o`)

| schema                         | what it is                                                 |
|--------------------------------|------------------------------------------------------------|
| `srt://<host>:<port>`          | SRT peer, calls out (caller), repeat to bond               |
| `srt://@<host>:<port>`         | SRT peer, binds/listens/accepts (listener), repeat to bond |
| `rtp://@<group>:<port>`        | RTP wrapped SPTS multicast                                 |
| `udp://@<group>:<port>`        | plain SPTS multicast                                       |
| `http://<host>:<port>/<path>`  | HTTP TS stream, `-i` only                                  |
| `https://<host>:<port>/<path>` | same, TLS (`-k` skips verification), `-i` only             |
| `-`                            | stdin (`-i`) or stdout (`-o`)                              |
| `<path>`                       | a file                                                     |

Exactly one of `-i`/`-o` must be `srt://`. `<host>` is a numeric IP, not a hostname. 
SRT's caller/listener role is independent of which side produces the media: 
add an `@` right after `srt://` to listen, leave it off to call out,
regardless of whether that side is `-i` or `-o`.

Repeating `-i`/`-o` with further `srt://` URIs bonds several links (`--group-mode
broadcast|backup`). Every bonded peer of one endpoint must agree on caller vs. listener.

`--rendezvous` lets both ends actively dial each other without a listener (NAT-to-NAT links).
It needs `--local` and cannot be combined with `srt://@` or `--group-mode`. 

## Examples

```sh
# sender side, caller
dipisrt -i rtp://@239.1.1.1:5000 -o srt://1.2.3.4:9000 --latency 200

# receiver side, listener
dipisrt -i srt://@0.0.0.0:9000 -o rtp://@239.1.1.1:5000

# bonded sender: two SRT links carrying the same stream
dipisrt -i rtp://@239.1.1.1:5000 -o srt://1.2.3.4:9000 -o srt://5.6.7.8:9000 --group-mode broadcast

# encrypted
dipisrt -i rtp://@239.1.1.1:5000 -o srt://1.2.3.4:9000 --passphrase correcthorsebatterystaple
```

## Notes

* `dipisrt` only works in one direction per process.
* Bonding (`--group-mode`): 
  + Most shipped binaries of libsrt have this disabled.
    `dipisrt` detects this at runtime and enables the feature only if the linked library supports it.
    You are encouraged to carry out your own testing before using it for anything that matters.
  + If you wondered why the automated bonding tests (GitHub Actions, latest tests, bonding-tests) don't test
    recovery after a 100% link loss (but 95% instead): libsrt doesn't do that. Ever.
* FEC (`--packetfilter`) is off by default, matching libsrt's own default.
* The sender's outgoing queue is sized automatically from the observed input bitrate,
  `--latency`, and `--send-buffer-mult`, so it can absorb a struggling link without
  dropping data. It never shrinks below roughly 84KB, never grows past about 2% of
  available RAM, and never discards data that's still actually queued.
