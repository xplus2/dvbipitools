# dipibcg

DVB-IPI EPG/BCG (ETSI TS 102 539). Announce an xmltv guide on multicast as BiM-encoded TVA
fragments ([dipixmltv](../dipixmltv/README.md) + [dipibim](../dipibim/README.md) under the hood),
or listen for one and write xmltv back.

```sh
dipibcg -a -i <xmltv> -M <map.csv> -m <mcast>:<port> [options]
dipibcg -l -m <mcast>:<port> [options]
```

## Announce (`-a`)

Reads `-i` (xmltv) and `-M` (`id,uri,tsid,onid,sid` csv, same shape as dipixmltv/dipiscan) at
startup, and again on SIGHUP (see [Reloading](#reloading-announce)). Every `-t`/`--interval`
seconds (default 5): re-filters programmes against the current time, BiM-encodes what's left,
wraps it in a container (one binary + one string data repository, TS 102 822-3-2 4.5.2.1) and sends
via DVBSTP payload 0xA3. No TVAMain fragment - it's optional (TS 102 323 9.4.2.2) and pure overhead
once every fragment carries its own type tag.

`-w`/`--window` hours (default 24): only programmes starting within that window are included; past
programmes drop off on their own as the wall clock moves, no need to re-read `-i`.

## Listen (`-l`)

Joins `-m`, reassembles DVBSTP segments, and after `-t`/`--timeout` seconds (default 35) writes the
most recently decoded container as xmltv to `-o` (default stdout). Each cycle is a complete
snapshot, not incremental, so listen keeps the latest rather than merging across cycles.

`-C`/`--csv-map` also writes a companion mapping csv (real uri/tsid/onid/sid from the received
ServiceInformation fragments), feedable straight back into a future `-M`.

## Network interface (`-I`)

Picks the interface for the multicast join/send. Default: kernel route.

## Live stats (`-v`)

Announce: one line per cycle. Listen: one line per segment.

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
