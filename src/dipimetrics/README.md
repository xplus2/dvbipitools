# dipimetrics

Host-level metrics collector for `dipitvhead`, `dipiradiohead`, `dipisds`, `dipibcg`, `dipirist`,
`dipirec`, `dipidescramble`, `dipicam378`, `dipifccret` and `dipixy`.
Each of those tools, if started with `--metrics-id`, periodically sends a snapshot of its own counters
over a Unix datagram socket. `dipimetrics` retains the latest snapshot per (component, instance),
and serves them all as one Prometheus/OpenMetrics document over plain HTTP.

```
dipimetrics [options]
```

## Options

| flag | long form          | argument              | default                             |
|------|--------------------|-----------------------|-------------------------------------|
| `-S` | `--sock`           | `<path>`              | `/run/dvbipitools/metrics.sock`     |
| `-l` | `--listen`         | `<addr>:<port>`       | `127.0.0.1:9109`                    |
|      | `--tls-cert`       | `<path>`              | off (plain HTTP)                    |
|      | `--tls-key`        | `<path>`              | off (plain HTTP)                    |
|      | `--auth`           | `<user>:<pass>`       | off, HTTP Basic Auth for `/metrics` |
| `-e` | `--expiry`         | `<s>`                 | `30`                                |
| `-v` | `--verbose`        |                       | off                                 |
|      | `--color`          | `auto\|always\|never` | `auto`                              |
| `-d` | `--daemonize`      |                       | off (foreground)                    |
| `-c` | `--config`         | `<path>`              | `/etc/dvbipitools/dipimetrics.yaml` |
|      | `--config-strict`  |                       | config file issues are errors       |
|      | `--configtest`     |                       | check the config file, then exit    |
| `-h` | `--help`           |                       |                                     |

## Configuration file

All options (except `-h`, `-c`, `--config-strict` and `--configtest`) can be set in a YAML file, see [dipimetrics.yaml](dipimetrics.yaml).
Debian installs config examples to: `/usr/share/dvbipitools/etc/`.

Without `-c`, `/etc/dvbipitools/dipimetrics.yaml` is read if it exists.
`--configtest` checks the file and exits.

Configuration file issues (unknown or duplicate keys, invalid values, conflicting settings, referenced files
that are not readable) are reported as warnings and the affected entries are skipped or overridden (last one wins).

With `--config-strict` they are errors instead: all of them are listed and the tool fails early.

## How it works

- `-S`/`--sock` is a `SOCK_DGRAM` Unix socket. `dipimetrics` binds it and never blocks on it.
  A slow or absent collector never affects the exporters, which are already best-effort senders themselves.
- Each exporter sends periodic snapshots of its own counters. A snapshot replaces the previous one
  only once it has arrived completely, so `/metrics` never shows a half-updated instance.
  `dipimetrics` rejects anything malformed, oversized, or from an unsupported protocol version outright.
- Upgrade `dipimetrics` before the exporters: an older `dipimetrics` rejects snapshots from newer exporters.
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
a general-purpose web server. Authentication is off unless `--auth <user>:<pass>` is given.

`--tls-cert`/`--tls-key` (PEM, both required together) switch `-l` from plain HTTP to HTTPS.
The certificate is reloaded from the same paths on `SIGUSR1`, without dropping the listener or
any in-flight connection.

Output is `application/openmetrics-text`: one `# TYPE`/`# HELP` pair per metric family actually
present (families with zero live samples are omitted), samples labeled
`component="tvhead|radiohead|sds|bcg|rist|rec|descramble|cam378|fccret"` and
`headend_id="<the exporter's --metrics-id>"` plus whatever label the metric itself carries
(`reason`, `input`, `table`, `codec`, `transport`, `version`, `peer`, `output`, `mode`, `direction`, `stream`).
The label is deliberately not called `instance`: Prometheus assigns its own
`instance` label per scrape target (the `host:port` of `dipimetrics` itself), which would
collide with and rename an exporter-supplied `instance` label to `exported_instance`.
One extra, collector-computed series is added per tracked instance:
`dvbipi_metrics_snapshot_age_seconds`: seconds since that instance's last accepted snapshot,
independent of anything the exporter itself reports.

`dipimetrics` also reports on itself, always present regardless of what's currently tracked:
`dvbipi_metrics_instances` (gauge, exporter instances currently held in the store),
`dvbipi_metrics_snapshots_received_total` (counter), `dvbipi_metrics_snapshots_rejected_total{reason="malformed|stale|full|version|toolarge"}`
(counter, one series per reason), `dvbipi_metrics_snapshots_incomplete_total` (counter, snapshots discarded
because a piece of them was lost), `dvbipi_metrics_parts_orphaned_total` (counter, stray snapshot
pieces dropped) and `dvbipi_metrics_http_requests_total{status="200|404"}`
(counter, one series per status). Each exporter additionally reports `dvbipi_metrics_parts_dropped_total`
(counter, snapshot pieces it failed to send).

## Transport stream health (`dvbipi_ts_*`)

Tools started with `--metrics-inspect-ts <off|basic|medium|full>` (also `metrics.inspect-ts` in YAML, needs `--metrics-id`)
report transport-stream health per stream. labeled by `direction` (`input` or `output`) and `stream` (`input0`, `output0`, ...).

The inspection levels describe **runtime cost and amount of retained state**, not ETSI TR 101 290 priority classes.

* `off` disables TS inspection completely. This is the default.
* `basic` is intended to be inexpensive enough for normal always-on operation.
* `medium` and `full` progressively enable checks which require additional parsing, timing information or per-PID state.

Counters are cumulative since process start. Missing or late tables increment their counter once per error episode rather than once per polling interval.
`dvbipi_ts_*_timestamp_seconds` values are Unix timestamps; for example, PromQL can obtain the current age with `time() - dvbipi_ts_..._timestamp_seconds`.

These metrics are intended for continuous operational monitoring, not as a replacement for a transport-stream conformance analyzer.
This is not a conformance test: the checks follow ETSI TR 101 290 where they can be measured from a receiver,
and buffer models, arrival-time PCR accuracy and RF checks are not covered.

`dipifccret` and `dipixy` see many independent streams and report one aggregate `input0` with the header-level
checks only. `dipirist` and `dipisrt` report the input stream, and `dipisrt` in receiver mode the output too.
`dipiradiohead` reports its generated output, and an input for every HLS input, whose segments are transport streams.
`dipirec` reports its input with `-f raw` and `-f ts`, and with `-f ts` its filtered output too.
A continuity error can also come from packets lost on the receiving host itself (UDP socket or NIC
buffers) and not only from the source. The old unlabeled `dipitvhead` series with these names are replaced by the
labeled ones when the option is on.

## Live stats (`-v`)

Logs every rejected/dropped snapshot (malformed, unsupported protocol version, stale sequence, store full, oversized, orphan part)
and every `404`, with enough detail to diagnose a misbehaving exporter or a stray HTTP client.

## Signals

* `^C`, SIGINT or SIGTERM: stop
* SIGUSR1: reload the TLS certificate/key

## Running under systemd

See [dipimetrics.service](dipimetrics.service) for a reasonable starting point.

Make sure that the processes reporting to the metrics.sock have the according permissions to do so.
For example, use `User=dvbipitools` in their systemd units.

## Examples

```sh
# start the collector with defaults
dipimetrics

# reachable from another host, custom expiry
dipimetrics -l 0.0.0.0:9109 -e 60

# HTTPS
dipimetrics -l 0.0.0.0:9109 --tls-cert srv.crt --tls-key srv.key

# require HTTP Basic Auth on the scrape
dipimetrics -l 0.0.0.0:9109 --auth scraper:hunter2

# let dipitvhead report in
dipitvhead ... --metrics-id headend1-tv1 --metrics-interval 5

# cheap always-on TS health
dipitvhead ... --metrics-id headend-a --metrics-inspect-ts basic

# deeper inspection for troubleshooting
dipitvhead ... --metrics-id headend-a --metrics-inspect-ts full --metrics-inspect-ts-pids 0x100,0x101,0x102
```
