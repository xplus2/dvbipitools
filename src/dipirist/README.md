# dipirist

`dipirist` bridges a DVB-IPI stream between a plain RTP/UDP/file endpoint and a RIST link
(VSF TR-06), in either direction. The direction is detected, depending on `-i`/`-o` being a `rist://` URI.

Dependencies:
* `librist`

Usage:
```
dipirist -i <uri> -o <uri> [options]
```

## Options

| flag | long form            | argument              | default                            |
|------|----------------------|-----------------------|------------------------------------|
| `-i` | `--in`               | `<uri>`               | required                           |
| `-o` | `--out`              | `<uri>`               | required                           |
| `-I` | `--iface`            | `<iface>`             | kernel route (non-RIST side only)  |
| `-k` | `--insecure`         |                       | off (`-i https://` source only)    |
|      | `--profile`          | `simple\|main`        | `simple`                           |
|      | `--secret`           | `<psk>`               | none (requires `--profile main`)   |
|      | `--cname`            | `<name>`              | library default                    |
|      | `--buffer`           | `<ms>`                | library default                    |
|      | `--color`            | `auto\|always\|never` | `auto`                             |
|      | `--metrics`          | `<path>`              | `/run/dvbipitools/metrics.sock`    |
|      | `--metrics-id`       | `<name>`              | none (metrics disabled unless set) |
|      | `--metrics-interval` | `<s>`                 | `5`                                |
| `-v` | `--verbose`          |                       | off                                |
| `-d` | `--daemonize`        |                       | off (foreground)                   |
| `-h` | `--help`             |                       |                                    |

## Endpoints (`-i`/`-o`)

| schema                                       | what it is                                      |
|----------------------------------------------|-------------------------------------------------|
| `rist://<host>:<port>[?params]`              | RIST peer, calls out; `-o` only, repeat to bond |
| `rist://@<host>:<port>[?params]`             | RIST peer, listens; `-i` only, repeat to bond   |
| `rtp://@<group>:<port>`                      | RTP wrapped SPTS multicast                      |
| `udp://@<group>:<port>`                      | plain SPTS multicast                            |
| `http://<host>:<port>/<path>`                | HTTP TS stream, `-i` only                       |
| `https://<host>:<port>/<path>`               | same, TLS (`-k` skips verification), `-i` only  |
| `-`                                          | stdin (`-i`) or stdout (`-o`)                   |
| `<path>`                                     | a file                                          |

Exactly one of `-i`/`-o` must be `rist://`. Whether that peer listens or calls out is controlled the same way as 
this toolkit's own `rtp://@`/`udp://@` addresses: add an `@` right after `rist://` to listen, 
leave it off to call out.

That's librist's own URL syntax, its query parameters (`buffer`, `secret`, `cname`, `weight`, ...) work here too.
If you set `--secret`, `--cname`, or `--buffer` on the command line, those win over whatever a URI's own query parameters included.

## Examples

```sh
# sender side
dipirist -i rtp://@239.1.1.1:5000 -o rist://1.2.3.4:6000 --buffer 1000

# receiver side
dipirist -i rist://@0.0.0.0:6000 -o rtp://@239.1.1.1:5000 --buffer 1000

# bonded sender: two RIST links carrying the same stream
dipirist -i rtp://@239.1.1.1:5000 -o rist://1.2.3.4:6000 -o rist://5.6.7.8:6000
```

## Notes

* `dipirist` doesn't use RIST's OOB channel
* `dipirist` only works in one direction per process. 
