# dipibcg

DVB-IPI EPG/BCG (ETSI TS 102 539). Announce an xmltv guide on multicast as BiM-encoded TVA
fragments ([dipixmltv](../dipixmltv/README.md) + [dipibim](../dipibim/README.md) under the hood),
or listen for one and write xmltv back.

```sh
dipibcg -a -i <xmltv> -M <map.csv> -m <mcast>:<port> [options]
dipibcg -l -m <mcast>:<port> [options]
```

## Options

| flag | long form   | argument               | default                                    |
|------|-------------|-------------------------|--------------------------------------------|
| `-a` | `--announce`|                         | headend mode: read `-i`, transmit on `-m`  |
| `-l` | `--listen`  |                         | client mode: receive on `-m`, write `-o`   |
| `-i` | `--input`   | `<path>`                | announce: required                         |
| `-M` | `--map`     | `<path>`                | announce: required                         |
| `-w` | `--window`  | `<hours>`               | announce: `24`                             |
| `-m` | `--mcast`   | `<g>:<p>`               | required                                   |
| `-I` | `--iface`   | `<iface>`               | kernel route                               |
| `-t` | `--interval`| `<s>`                   | announce: `5`                              |
| `-t` | `--timeout` | `<s>`                   | listen: `35`                               |
| `-o` | `--output`  | `<path>` / `-`          | listen: `-` (stdout)                       |
| `-C` | `--csv-map` | `<path>`                | listen: off                                |
| `-Z` | `--compress`|                         | announce: off                              |
| `-v` | `--verbose` |                         | off                                        |
|      | `--color`   | `auto\|always\|never`   | `auto`                                     |
|      | `--metrics` | `<path>`                | announce: `/run/dvbipitools/metrics.sock`  |
|      | `--metrics-id` | `<name>`             | announce: none (metrics disabled unless set) |
|      | `--metrics-interval` | `<s>`          | announce: `5`                              |
| `-d` | `--daemonize` |                       | off (foreground)                           |
| `-h` | `--help`    |                         |                                             |

## Announce (`-a`)

Reads `-i` (xmltv) and `-M` (`id,uri,tsid,onid,sid` csv, same shape as dipixmltv/dipiscan) at
startup, and again on SIGHUP (see [Reloading](#reloading-announce)). Every `-t`/`--interval`
seconds (default 5): re-filters programmes against the current time, BiM-encodes what's left,
wraps it in a container (one binary + one string data repository, TS 102 822-3-2 4.5.2.1), wraps
*that* in a `compression_wrapper` (TS 102 323 clause 7.3.1.5) and sends via DVBSTP payload 0xA3
with `compr=001` (TS 102 539 table 3 - BiM/binary representation, mandatory for BCG payload ids).
No TVAMain fragment - it's optional (TS 102 323 9.4.2.2) and pure overhead once every fragment
carries its own type tag.

`-w`/`--window` hours (default 24): only programmes starting within that window are included; past
programmes drop off on their own as the wall clock moves, no need to re-read `-i`.

`-Z`/`--compress`: zlib-compresses the container inside the wrapper (RFC 1950, `compression_method
0x01`).

## Listen (`-l`)

Joins `-m`, reassembles DVBSTP segments, unwraps the `compression_wrapper` (inflating it first if
compressed), and after `-t`/`--timeout` seconds (default 35) writes the most recently decoded
container as xmltv to `-o` (default stdout). Each cycle is a complete snapshot, not incremental, so
listen keeps the latest rather than merging across cycles. A segment whose wrapper this build can't
decompress (no zlib) is skipped like any other malformed segment.

`-C`/`--csv-map` also writes a companion mapping csv (real uri/tsid/onid/sid from the received
ServiceInformation fragments), feedable straight back into a future `-M`.

## Network interface (`-I`)

Picks the interface for the multicast join/send. Default: kernel route.

## Live stats (`-v`)

Announce: one line per cycle. Listen: one line per segment.

## Metrics (`-a` only)

`--metrics-id <name>` turns on OpenMetrics export; without it, `--metrics`/`--metrics-interval`
are rejected and nothing is sent. `--metrics <path>` picks the Unix datagram socket a collector
like `dipimetrics` listens on (default `/run/dvbipitools/metrics.sock`), and `--metrics-interval
<s>` sets how often a snapshot goes out (default 5).

## Signals

* `^C`, SIGINT or SIGTERM: stop the process
* SIGHUP: re-read `-i` and `-M` from disk. On error, the previous guide keeps being announced and
the error is logged. (No effect in `-l` mode).


## Running under systemd

If `dipibcg` is meant to run continuously under systemd, the unit below is a reasonable starting
point for `-a` mode; `-l` mode works the same way, swapped for `-l`/`-o` and a writable path
instead of `ReadOnlyPaths`.

```ini
[Unit]
Description=dipibcg
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/dipibcg -a -i /etc/dipibcg/guide.xml -M /etc/dipibcg/mapping.csv -m 239.255.0.2:3938
Restart=on-failure
RestartSec=5
StartLimitIntervalSec=60
StartLimitBurst=10
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadOnlyPaths=/etc/dipibcg

[Install]
WantedBy=multi-user.target
```

## Examples

```sh
dipibcg -a -i guide.xml -M mapping.csv -m 239.255.0.2:3938
dipibcg -l -m 239.255.0.2:3938 -o guide.xml -C mapping.csv
```
