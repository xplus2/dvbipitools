# dipiepg

DVB-IPI EPG/BCG (ETSI TS 102 539). Announce an xmltv guide on multicast as BiM-encoded TVA
fragments ([dipixmltv](../dipixmltv/README.md) + [dipibim](../dipibim/README.md) under the hood),
or listen for one and write xmltv back.

```sh
dipiepg -a -i <xmltv> -M <map.csv> -m <mcast>:<port> [options]
dipiepg -l -m <mcast>:<port> [options]
```

## Announce (`-a`)

Reads `-i` (xmltv) and `-M` (`id,uri,tsid,onid,sid` csv, same shape as dipixmltv/dipiscan) once at
startup. Every `-t`/`--interval` seconds (default 5): re-filters programmes against the current
time, BiM-encodes what's left, wraps it in a container (one binary + one string data repository,
TS 102 822-3-2 4.5.2.1) and sends via DVBSTP payload 0xA3. No TVAMain fragment - it's optional
(TS 102 323 9.4.2.2) and pure overhead once every fragment carries its own type tag.

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

## Stopping

`^C`, SIGINT or SIGTERM.

## Some examples

```sh
dipiepg -a -i guide.xml -M mapping.csv -m 239.255.0.2:3938
dipiepg -l -m 239.255.0.2:3938 -o guide.xml -C mapping.csv
```
