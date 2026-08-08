# dipiscan

Scan a multicast /24 (or analogous IPv6 range) for DVB-IPI services, write a playlist of what answered.

```sh
dipiscan [options] 1>playlist 2>log
```

## Options

| flag | long form     | argument                    | default                          |
|------|---------------|-----------------------------|----------------------------------|
| `-m` | `--mcast`     | `<addr>`                    | `239.19.75.0`                    |
| `-p` | `--port`      | `<port[-port]>`             | `8700`                           |
| `-f` | `--format`    | `m3u\|csv\|xspf\|xml\|null` | `m3u`                            |
| `-P` | `--provider`  | `<name>`                    | required with `-f xml`           |
| `-o` | `--out`       | `<path>` / `-`              | `-` (stdout)                     |
| `-t` | `--timeout`   | `<secs>`                    | `1`                              |
| `-M` | `--mpts`      |                             | off (SPTS or first program only) |
| `-u` | `--udpxy`     | `<ip:port>`                 | off (direct IGMP/MLD join)       |
| `-I` | `--iface`     | `<iface>`                   | kernel default                   |
| `-v` | `--verbose`   |                             | off                              |
|      | `--color`     | `auto\|always\|never`       | `auto`                           |
| `-h` | `--help`      |                             |                                  |

## Scan range (`-m`, `-p`)

`-m <addr>` base multicast group, IPv4 or IPv6; last byte is swept 1..254 (/24). Default `239.19.75.0`.
`-p <port[-port]>` is a port or inclusive port range. Default `8700`.

Every address/port combination in range gets probed once.

## Output (`-o`, `-f`)

`-o <path>` is a file path, or `-` for stdout (the default). Progress and `-v` diagnostics always go to stderr.

`-f` can be ...

| format | description                                        |
|--------|----------------------------------------------------|
| `m3u`  | `#EXTM3U` playlist, `#EXTINF` name + URI per entry |
| `csv`  | `name,uri,tsid,onid,sid` per line                  |
| `xspf` | XSPF playlist                                      |
| `xml`  | SD&S Broadcast Discovery (ETSI TS 102 034)         |
| `null` | no playlist output, just the stderr log            |

Every format also carries the DVB triplet (`transport_stream_id`, `original_network_id`, `service_id`)
read from PAT/SDT: `csv` as three extra columns, `m3u` as `tsid`/`onid`/`sid` attributes on the
`#EXTINF` line, `xspf` as a `<extension application="urn:dvbipitools:dvb-triplet">` element per
track, `xml` as the `DVBTriplet` element the schema already defines for it.

`-f xml` also needs `-P <name>` (`--provider`): the schema's `DomainName` is required, and dipiscan
has no identity of its own to put there - the scan output isn't claiming to be a service provider,
it's just reporting what it found under whatever name you give it. Feed the result straight into
`dipisds -a -i scan.xml -m ...` to announce it.

## Probe time budget (`-t`)

`-t <secs>` deadline budget per candidate for a named result (PAT + SDT service name). Default 1 second.

If an address does not produce within 300 ms after the IGMP join, it gets skipped.

## Multi Program Transport Stream support (`-M`)

Without `-M`, one address is reported as one program: whichever program's PMT resolves
first (an MPTS address still answers, just with only one of its programs found).

With `-M`, every program the PAT lists at that address gets its own playlist entry
(same URI, distinct `sid`/name). This changes what `-t` waits for: instead of exiting
as soon as any one program is named, the probe now waits for every PAT-listed
program to be named, up to the full `-t` budget - same per-program bar as the
single-program probe, just applied to all of them at once. A program whose PMT
resolved but whose SDT name didn't arrive in time still gets a row, as `(no SDT)`;
one whose PMT never resolved at all is left out of that pass entirely. Raise `-t`
if a scanned mux carries several programs and rows go missing or come back
unnamed.

## udpxy (`-u`)

`-u <ip:port>` probes through a udpxy gateway instead of joining the group directly.
Port defaults to 80 if omitted.

## Network interface (`-I`)

Picks the interface for multicast joins. Without it, the kernel's default multicast route is used,
which is usually wrong in multi-homing.

## Live diagnostics (`-v`)

Prints packet counts per candidate on stderr, in addition to the name.

## Stopping

`^C` SIGINT or SIGTERM stops the scan after the current candidate and closes the playlist properly.

## Examples

```sh
# default range, m3u to stdout
dipiscan >channels.m3u

# custom range and port span
dipiscan -m 239.19.75.0 -p 8700-8705 >hd.m3u

# csv with per-candidate packet counts on stderr
dipiscan -v -f csv -o scan.csv

# through a udpxy gateway
dipiscan -u 127.0.0.1:8080 -m 239.19.75.0 -f xspf >playlist.xspf

# SD&S xml, ready for dipisds
dipiscan -f xml -P example.org -o scan.xml
dipisds -a -i scan.xml -m 239.255.0.1:3937

# MPTS-aware scan: one row per program per address
dipiscan -M -t 3 -f xml -P example.org -o scan.xml
```
