# dipimetrics

Host-level metrics collector for `dipitvhead`, `dipiradiohead`, `dipisds`, `dipibcg`, `dipirist`,
`dipirec`, `dipidescramble`, `dipicam378` and `dipifccret`. 
Each of those tools, if started with `--metrics-id`, periodically sends a snapshot of its own counters
over a Unix datagram socket. `dipimetrics` retains the latest snapshot per (component, instance),
and serves them all as one Prometheus/OpenMetrics document over plain HTTP.

```
dipimetrics [options]
```

## Options

| flag | long form     | argument              | default                         |
|------|---------------|-----------------------|---------------------------------|
| `-S` | `--sock`      | `<path>`              | `/run/dvbipitools/metrics.sock` |
| `-l` | `--listen`    | `<addr>:<port>`       | `127.0.0.1:9109`                |
| `-e` | `--expiry`    | `<s>`                 | `30`                            |
| `-v` | `--verbose`   |                       | off                             |
|      | `--color`     | `auto\|always\|never` | `auto`                          |
| `-d` | `--daemonize` |                       | off (foreground)                |
| `-h` | `--help`      |                       |                                 |

## How it works

- `-S`/`--sock` is a `SOCK_DGRAM` Unix socket. `dipimetrics` binds it and never blocks on it. 
  A slow or absent collector never affects the exporters, which are already best-effort senders themselves.
- Each datagram is a self-contained snapshot: a header (component, the exporter's own
  `--metrics-id`, its process start time, a per-process sequence number, and the snapshot's own
  timestamp) followed by its metric entries. `dipimetrics` rejects anything malformed, oversized, or
  from an unsupported protocol version outright.
- A snapshot with a lower-or-equal sequence number than the last one seen for the same
  (component, instance) is dropped as stale/out of order. A *different* process start time is treated as the exporter
  having restarted, and is always accepted, replacing all prior state for that instance.
- An instance that hasn't sent a snapshot in `-e`/`--expiry` seconds (default 30, i.e. 6 missed sends at an exporter's 
  own default 5s interval) is dropped entirely - it stops appearing in `/metrics`, rather than serving an arbitrarily stale last-known value.
- Up to 64 concurrently tracked instances; a 65th distinct (component, instance) pair is dropped and logged under `-v`.

## HTTP endpoint

`-l`/`--listen` is the address:port `GET /metrics` is served on, default `127.0.0.1:9109` (loopback
only, pass e.g. `-l 0.0.0.0:9109` for "any"). Every other path returns `404`. 
The server is intentionally minimal: one request handled at a time, `Connection: close` on every response, 
a several-second read/write budget per connection so a stalled client can't wedge the collector. 
This is a local diagnostics endpoint meant for infrequent scraping, not
a general-purpose web server. There is no TLS and no authentication.

Output is `application/openmetrics-text`: one `# TYPE`/`# HELP` pair per metric family actually
present (families with zero live samples are omitted), samples labeled
`component="tvhead|radiohead|sds|bcg|rist|rec|descramble|cam378|fccret"` and
`headend_id="<the exporter's --metrics-id>"` plus whatever label the metric itself carries
(`reason`, `input`, `table`, `codec`, `transport`, `version`, `peer`, `output`, `mode`). The label is deliberately not called `instance`: Prometheus assigns its own
`instance` label per scrape target (the `host:port` of `dipimetrics` itself), which would
collide with and rename an exporter-supplied `instance` label to `exported_instance`.
One extra, collector-computed series is added per tracked instance:
`dvbipi_metrics_snapshot_age_seconds`: seconds since that instance's last accepted snapshot,
independent of anything the exporter itself reports.

`dipimetrics` also reports on itself, always present regardless of what's currently tracked:
`dvbipi_metrics_instances` (gauge, exporter instances currently held in the store),
`dvbipi_metrics_snapshots_received_total` (counter), `dvbipi_metrics_snapshots_rejected_total{reason="malformed|stale|full|version"}`
(counter, one series per reason) and `dvbipi_metrics_http_requests_total{status="200|404"}`
(counter, one series per status).

## Live stats (`-v`)

Logs every rejected/dropped snapshot (malformed, unsupported protocol version, stale sequence, store full)
and every `404`, with enough detail to diagnose a misbehaving exporter or a stray HTTP client.

## Signals

`^C`, SIGINT or SIGTERM: stop the process (closes sockets, removes the Unix socket file).

## Running under systemd

Make sure that the processes reporting to the metrics.sock have the according permissions to do so.

For example, use `User=dvbipitools` in their systemd units.

```ini
[Unit]
Description=dipimetrics
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=dvbipitools
RuntimeDirectory=dvbipitools
ExecStart=/usr/bin/dipimetrics --sock /run/dvbipitools/metrics.sock --listen 127.0.0.1:9109
Restart=on-failure
RestartSec=5
StartLimitIntervalSec=60
StartLimitBurst=10
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
```

## Examples

```sh
# start the collector with defaults
dipimetrics

# reachable from another host, custom expiry
dipimetrics -l 0.0.0.0:9109 -e 60

# let dipitvhead report in
dipitvhead ... --metrics-id headend1-tv1 --metrics-interval 5
```
