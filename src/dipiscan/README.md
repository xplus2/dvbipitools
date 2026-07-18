# dipiscan

Scan a multicast /24 (or analogous IPv6 range) for DVB-IPI services, write a playlist of what answered.

```sh
dipiscan [options] 1>playlist 2>log
```

## Scan range (`-m`, `-p`)

`-m <addr>` base multicast group, IPv4 or IPv6; last byte is swept 1..254 (/24). Default `239.2.16.0`.
`-p <port[-port]>` is a port or inclusive port range. Default `8208`.

Every address/port combination in range gets probed once.

## Output (`-o`, `-f`)

`-o <path>` is a file path, or `-` for stdout (the default). Progress and `-v` diagnostics always go to stderr.

`-f` can be ...

| format | description                                        |
|--------|----------------------------------------------------|
| `m3u`  | `#EXTM3U` playlist, `#EXTINF` name + URI per entry |
| `csv`  | `name,uri` per line                                |
| `xspf` | XSPF playlist                                      |
| `null` | no playlist output, just the stderr log            |

## Probe time budget (`-t`)

`-t <secs>` deadline budget per candidate for a named result (PAT + SDT service name). Default 1 second.

If an address does not produce within 300 ms after the IGMP join, it gets skipped.


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

## Some examples

```sh
# default range, m3u to stdout
dipiscan >channels.m3u

# custom range and port span
dipiscan -m 239.2.24.0 -p 8208-8229 >hd.m3u

# csv with per-candidate packet counts on stderr
dipiscan -v -f csv -o scan.csv

# through a udpxy gateway
dipiscan -u 127.0.0.1:8080 -m 239.2.16.0 -f xspf >playlist.xspf
```
