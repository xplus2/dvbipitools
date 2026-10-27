# dipibcg

DVB-IPI EPG/BCG (ETSI TS 102 539). Announce TV-Anytime fragments on multicast, BiM-encoded or as plain XML,
from xmltv, TVA XML or BiM.

Or listen for them and write the received guide as xmltv, TVA XML or BiM.
It reads xmltv like [dipixmltv](../dipixmltv/README.md) and shares its BiM engine with [dipibim](../dipibim/README.md).

## Usage
```sh
dipibcg -a -s <schema> -i <input> [-M <map.csv>] -m <mcast>:<port> [options]
dipibcg -l -s <schema> -m <mcast>:<port> [options]
```

## Options

| flag | long form            | argument              | default                                      |
|------|----------------------|-----------------------|----------------------------------------------|
| `-a` | `--announce`         |                       | headend mode: read `-i`, transmit on `-m`    |
| `-l` | `--listen`           |                       | client mode: receive on `-m`, write `-o`     |
| `-f` | `--format`           | `xmltv\|tva\|bim`     | announce: input, listen: output, `xmltv`     |
|      | `--encoding`         | `bim\|plain`          | announce: `bim`                              |
| `-i` | `--input`            | `<path>`              | announce: required                           |
| `-M` | `--map`              | `<path>`              | announce: required for `-f xmltv`            |
| `-w` | `--window`           | `<hours>`             | announce: `24`                               |
| `-m` | `--mcast`            | `<g>:<p>`             | required                                     |
| `-I` | `--iface`            | `<iface>`             | kernel route                                 |
|      | `--dscp`             | `<v>`                 | announce: `signalling`                       |
| `-T` | `--ttl`              | `<n>`                 | announce: `16` (1..255)                      |
| `-t` | `--interval`         | `<s>`                 | announce: `5`                                |
|      | `--refresh`          | `<n>`                 | announce: `1`                                |
| `-t` | `--timeout`          | `<s>`                 | listen: `35`, `0` until signal               |
| `-o` | `--output`           | `<path>` / `-`        | listen: `-` (stdout)                         |
| `-C` | `--csv-map`          | `<path>`              | listen: off                                  |
| `-Z` | `--compress`         |                       | announce: off                                |
|      | `--publisher`        | `<domain>/<name>`     | announce: off (no TVAMain sent)              |
|      | `--lang`             | `<xs:language>`       | announce: off, requires `--publisher`        |
|      | `--index`            | `<kind>[,<kind>...]`  | announce: off (no index containers)          |
|      | `--dvb-locator`      |                       | announce: off                                |
|      | `--dvb-duration`     |                       | announce: off                                |
|      | `--dvb-pubtime`      |                       | announce: off                                |
| `-s` | `--schema`           | `<path>`              | required, repeatable                         |
| `-v` | `--verbose`          |                       | off                                          |
|      | `--color`            | `auto\|always\|never` | `auto`                                       |
|      | `--metrics`          | `<path>`              | announce: `/run/dvbipitools/metrics.sock`    |
|      | `--metrics-id`       | `<name>`              | announce: none (metrics disabled unless set) |
|      | `--metrics-interval` | `<s>`                 | announce: `5`                                |
| `-d` | `--daemonize`        |                       | off (foreground)                             |
| `-c` | `--config`           | `<path>`              | `/etc/dvbipitools/dipibcg.yaml`              |
|      | `--config-strict`    |                       | config file issues are errors                |
|      | `--configtest`       |                       | check the config file, then exit             |
| `-h` | `--help`             |                       |                                              |
|      | `--version`          |                       | print version and exit                       |

## Configuration file

All options (except `-h`, `-c`, `--config-strict`, `--configtest` and `--version`) can be set in a YAML file, see [dipibcg.yaml](dipibcg.yaml).
Debian installs config examples to: `/usr/share/dvbipitools/etc/`.

Without `-c`, `/etc/dvbipitools/dipibcg.yaml` is read if it exists.
`--configtest` checks the file and exits.

Configuration file issues (unknown or duplicate keys, invalid values, conflicting settings, referenced files
that are not readable) are reported as warnings and the affected entries are skipped or overridden (last one wins).

With `--config-strict` they are errors instead: all of them are listed and the tool fails early.

## Schema (`-s`)

TV-Anytime schema files and directories.
A directory is searched recursively for the `.xsd` declaring the namespace:
announce uses `urn:tva:metadata:2004`, listen the `SchemaURI[0]` of the stream.
One schema file is used as is, its own namespace is sent as `SchemaURI[0]`.
Of several the one declaring the namespace is used.
Further schemas listed in the DecoderInit of a received or `-f bim` input stream are found in the same files and directories,
the chunks of those not found are skipped.

Announce lists the namespaces of a `-f tva` document that the main schema does not define (elements, attributes, `xsi:type`
values) as further schemas of the DecoderInit, each found in the same files and directories, and codes their content 
as chunks of them.

A namespace without schema file stays with the main schema and is not listed.
Download links and SHA-256 of the TV-Anytime XSDs: [dipibim](../dipibim/README.md#required-xsds).
Imports and includes resolve in the directories and next to the schema file.

## Announce (`-a`)

`-f` names the input at `-i`: `xmltv` (default, with `-M`, a `id,uri,tsid,onid,sid` csv of the same shape as dipixmltv/dipiscan),
`tva` (a TVAMain document) or `bim` (a stream written by [dipibim](../dipibim/README.md) `--profile dvb-tva`). 
Of xmltv channels with the same id the first is used, the others are dropped with a warning. Channels missing from `-M`
are not announced, also with a warning.
`tva` and `bim` input must meet the TS 102 323 clause 8 content rules (as dipibim checks them), a violation fails the load.
The input is read at startup, and again on SIGHUP (see [Signals](#signals)). With `xmltv`, every `-t`/`--interval`
seconds (default 5): re-filters programmes against the current time (a programme without stop ends at the next start
on its channel, the last one without stop is left out, TS 102 323 8.5), BiM-encodes what's left,
wraps it in a container (TS 102 323 9.3 encapsulation, one binary + one string data repository, TS 102 822-3-2 4.5.2.1), 
wraps *that* in a `compression_wrapper` (TS 102 323 clause 7.3.1.5) and sends via DVBSTP payload 0xA3
with `compr=001` (TS 102 539 table 3 - BiM/binary representation, mandatory for BCG payload ids).
Each channel is its own container and DVBSTP segment (segment_id 1..n in channel order), each with its own version: a
changed programme bumps only its channel's segment. Fragment pointers are 16 bit, so a single channel over 65535 bytes per
repository is an error. With more than 65535 channels, channels share segments up to that limit.
A guide that fails to build is fatal at startup. Later, the last built guide keeps being sent and the build is retried
every cycle, its errors logged once until a build succeeds again.
No TVAMain fragment - it's optional (TS 102 323 9.4.2.2) and pure overhead once every fragment
carries its own type tag.

Every Schedule fragment carries `start`/`end` covering its events, events are in chronological order and use
`PublishedDuration`, no `PublishedEndTime` (TS 102 323 8.5).

### TVAMain (`--publisher`, `--lang`)

With `--publisher` each cycle also sends the TVAMain fragment (TS 102 539 7.4) as a data container on payload 0xA2:
the default TVAMain skeleton of TS 102 323 table 55 (tables the namespace's schema has) plus `publisher` and, with
`--lang`, `xml:lang`. The publisher format is `<domain>/<readable name>` with the name RFC 3986 encoded
(TS 102 323 8.9), like `example.com/My%20Guide`. 
Listen mode decodes 0xA2 and writes the readable name as `<tv source-info-name>` and `xml:lang` as `lang`
on `title`, `desc` and `category`.

### Index containers (`--index`)

`--index` builds the TS 102 323 9.5.1 profiled indices on the fragments sent: `pi-crid` (ProgramInformation by
CRID), `pi-title` (ProgramInformation by title), `schedule-time` (Schedule by end time and service, two layers),
`schedule-title` (Schedule by event title; adds `InstanceDescription/Title` to every ScheduleEvent). 
GroupInformation indices don't apply, no GroupInformation is sent.

Listen mode stores 0xA4 and 0xA5 containers (0xA5 data part decoded like 0xA3), checks every profiled index of the
index list (structures present, entries ordered, every locator pointing to a received fragment) and warns on problems.
Other index list entries are ignored (TS 102 323 9.5.1.3).

Announce encodes against the schema of `urn:tva:metadata:2004`, or the schema file given with `-s`
, no length-coded extension chunks.

Listen takes the namespace from the DVB-TVA-init (0xA1) and loads that schema through `-s`; with a schema file, a stream of another
namespace is ignored with `[GW400240]`. Fragments of types the schema does not
code are skipped. ServiceInformation carries
one ServiceURL, so `-M` onid/tsid/sid and a no-name flag follow its own strings in its string buffer, where
receivers don't look.

With `tva` and `bim` the document is split into the fragment types of TS 102 822-3-2 table 7 and the fragments are 
filled into containers up to the repository limit, a fragment keeps its id and version as long as its content is unchanged.
Its TVAMain fragment is not sent, `--publisher` builds one. There is no `-w`/`--window` and no `--index`.

`--encoding plain` sends the same payload ids with `compr=000` and the fragments of each container as one TVA XML document.
No DVB-TVA-init (0xA1), no index containers, no fragment ids and the segments carry no `-M` onid/tsid/sid.

`-w`/`--window` hours (default 24, `xmltv` only): only programmes starting within that window are included; past
programmes drop off on their own as the wall clock moves, no need to re-read `-i`.

`-Z`/`--compress`: zlib-compresses the container inside the wrapper (RFC 1950, `compression_method
0x01`).

## Listen (`-l`)

Joins `-m`, reassembles DVBSTP segments, unwraps the `compression_wrapper` (inflating it first if
compressed), and after `-t`/`--timeout` seconds (default 35) writes the most recently decoded
container of each segment id, merged, to `-o` (default stdout).
Segments with `compr=001` are BiM, with `compr=000` plain TVA XML
(a schema namespace is taken from the XML root). `-f xmltv` (default) writes xmltv, `tva` one TVA XML document, `bim` a
[dipibim](../dipibim/README.md) `--profile dvb-tva` stream of the received fragments.

Each cycle is a complete snapshot, so a newer version of a segment replaces its older one.

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

## Specification conformance

dipibcg conforms to ETSI TS 102 539 (BCG over IP) with BiM coded per ETSI TS 102 323 on the ISO/IEC 15938-1:2002 payload syntax.
Per layer:

| layer              | specification                                                                                         |
|--------------------|-------------------------------------------------------------------------------------------------------|
| transport          | ETSI TS 102 539 (BCG over IP), DVBSTP payload ids 0xA1 to 0xA5, `compr=001` for BiM                    |
| BiM coding         | TS 102 323 9.4 (DVB-TVA-init, DVBBiMAccessUnit, DVB codecs) on the ISO/IEC 15938-1:2002 payload syntax |
| containers         | TS 102 323 9.3 and TS 102 822-3-2 4.5, `compression_wrapper` of TS 102 323 7.3.1.5                      |
| index containers   | TS 102 323 9.5.1 (`--index`)                                                                          |

The BiM here is what [dipibim](../dipibim/README.md#specification-conformance) writes with `--profile dvb-tva`.

`--dvb-locator`, `--dvb-duration` and `--dvb-pubtime` select optional codings of TS 102 323 9.4.3.

The MPEG-7 Systems conformance streams are MPEG-7 schema streams without DVB-TVA-init or BCG transport, they decode with dipibim only.

## Signals

* `^C`, SIGINT or SIGTERM: stop the process
* SIGHUP: re-read `-i` and `-M` from disk. On error, also when the new guide fails to build, the previous guide keeps being announced and
the error is logged. It has no effect in `-l` mode.

## Running under systemd

See [dipibcg.service](dipibcg.service) for a reasonable starting point (announce mode).
`-l` listening mode works the same way, swapped for `-l`/`-o` and a writable path
instead of `ReadOnlyPaths`.

## Examples

```sh
dipibcg -a -i guide.xml -M mapping.csv -m 239.255.0.2:3938
dipibcg -l -m 239.255.0.2:3938 -o guide.xml -C mapping.csv
```
