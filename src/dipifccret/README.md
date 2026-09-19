# dipifccret

An RTP Retransmission (RET) and Server-based Fast Channel Change (FCC) **edge server**,
DVB-IPI Annex F (ETSI TS 102 034) and Annex I, built on IETF RFC 6285 (RAMS - Rapid
Acquisition of Multicast RTP Sessions).

Passively watches a mirrored/SPAN-ported multicast segment via a raw AF_PACKET capture, then keeps two
kinds of per-channel state from the same captured packets: a short seq-keyed ring for RET loss repair, 
and a Random Access Point (RAP)-anchored cache for FCC bursts. One capture thread and one `-l` listen socket 
serve both protocols. A client NACK always gets a direct unicast reply *and*, when the multicast RET session 
is enabled, is additionally repaired over that session (F.5.2); a RAMS-R request just before a channel join 
gets a burst of cached data at a faster rate so the client can start decoding immediately.

RET and FCC can each be run alone or together: `--no-ret` disables RET, `--no-fcc` disables FCC.

Small deployments may run `dipifccret` just once as a headend companion, larger ones might
want distributed deployments.

## Usage

```
dipifccret -g <range> -l <addr>:<port> -I <iface> [options]
```

## Options
| flag | long form               | argument    | default                            | description                                         |
|------|-------------------------|-------------|------------------------------------|-----------------------------------------------------|
| -g   | --range                 | cidr[,...]  | all                                | multicast range(s) to capture                       |
| -l   | --listen                | addr:<port  |                                    | unicast bind, shared by RET and FCC traffic         |
| -I,  | --iface                 | iface       |                                    | capture interface (required, single Ethernet NIC)   |
| -M   | --max-channels          | n           |                                    | 300. preallocated channel slots                     |
|      | --channel-idle-timeout  | s           | 120                                | free channel slot after silent seconds (0 = never)  |
| -R   | --rtx-pt                | n           | 99                                 | RTP payload type for retransmitted/burst packets    |
| -w   | --workers               | n           | <CPU cores>                        | -l socket worker threads                            |
| -u   | --user                  | user        | off                                | drop privileges to this user                        |
| -v   | --verbose               |             | off                                | periodic stats on stderr                            |
|      | --color                 | when        | auto                               | auto\|always\|never                                 |
|      | --metrics               | path        | `/run/dvbipitools/metrics.sock`    | Unix datagram socket for metrics                    |
|      | --metrics-id            | name        |                                    | stable instance id, metrics disabled unless set     |
|      | --metrics-interval      | s           | 5                                  | snapshot interval in seconds                        |
| -d   | --daemonize             |             |                                    | fork to background after startup                    |
| -c   | --config                | path        | `/etc/dvbipitools/dipifccret.yaml` | YAML config file                                    |
|      | --config-strict         |             |                                    | config file issues are errors                       |
|      | --configtest            |             |                                    | check the config file, then exit                    |
| -h   | --help                  |             |                                    | prints help                                         |


### RET (Annex F)
| flag | long form                 | argument | default       | description                                      |
|------|---------------------------|----------|---------------|--------------------------------------------------|
|      | --no-ret                  |          |               | disable RET                                      |
| -B   | --buffer                  | <ms>     | 2000          | per-channel retransmission buffer size           |
| -F   | --ff-port                 | <port>   | original port | multicast RET session port                       |
|      | --no-mc-ret               |          |               | disable multicast RET session                    |
|      | --max-ret-clients         | <n>      | 16384         | pre-allocated unicast RTX client slots, F.3.2.1  |
|      | --ret-client-idle-timeout | <s>      | 300           | free RTX client slot after X seconds (0 = never) |
|      | --no-rsi                  |          |               | disable RSI self-announcement                    |
|      | --rsi-interval            | <s>      | 5             | RSI self-announcement interval (seconds)         |
|      | --rsi-mc-ret              |          |               | RSI rides the MC RET session, needs MC RET       |
|      | --rsi-hostname            | <name>   | -l address    | announce this hostname (SRBT 2) in RSI           |


### FCC (Annex I)
| flag | long form                   | argument   | default   | description                                                              |
|------|-----------------------------|------------|-----------|--------------------------------------------------------------------------|
|      | --no-fcc                    |            |           | disable FCC                                                              |
| -G   | --gop-cap                   | ms         | 8000      | safety cap on cached GOP-in-progress duration                            |
| -C   | --max-bursts                | n          | 4096      | preallocated concurrent burst-session slots                              |
| -X   | --burst-multiplier          | n          | 1.5       | burst rate as multiple of observed nominal bitrate                       |
| -D   | --burst-duration-cap        | ms         | 10000     | hard max burst duration regardless of signaling                          |
|      | --max-buffer-fill-bound     | ms         | 30000     | reject a RAMS-R Min RAMS Buffer Fill Requirement above this (0 = none)   |
|      | --fcc-resolve-by-port       |            | off       | resolve ignore-media-ssrc RAMS-R by dedicated per-channel port           |
|      | --fcc-resolve-base-port     | port       | -l port+1 | base port for --fcc-resolve-by-port                                      |
|      | --congestion-nack-threshold | n          | 5         | NACKs during one burst before terminating it as congested (0 = disabled) |
|      | --fcc-range                 | cidr[,...] | all of -g | restrict FCC to these -g sub-ranges                                      |
|      | --fcc-client-range          | cidr[,...] | any       | restrict FCC requests to these client source ranges                      |


## Configuration file

All options (except `-h`, `-c`, `--config-strict` and `--configtest`) can be set in a YAML file, see [dipifccret.yaml](dipifccret.yaml).
Debian installs config examples to: `/usr/share/dvbipitools/etc/`.

Without `-c`, `/etc/dvbipitools/dipifccret.yaml` is read if it exists.
`--configtest` checks the file and exits.

Configuration file issues (unknown or duplicate keys, invalid values, conflicting settings, referenced files
that are not readable) are reported as warnings and the affected entries are skipped or overridden (last one wins).

With `--config-strict` they are errors instead: all of them are listed and the tool fails early.

## Why passive capture, not an IGMP join

Joining every channel would mitigate all benefits of multicast distribution.
A mirror/SPAN port gives a read-only copy of exactly what's actually consumed, with zero footprint
on the distribution tree for the channels themselves.

`-g` is the authoritative multicast range whitelist, enforced in userspace regardless of the
installed kernel filter. Channels are discovered dynamically within, not configured one by one.

The kernel-side pre-filter (built from `-g`) always unwraps a single VLAN tag before matching,
so 802.1Q-tagged trunk mirror ports need no separate configuration.

## Multicast RET session

Per F.6.2.2, the repair session reuses the _same_ destination group:port as the original
channel. SSM already distinguishes it by source address, so no separate address scheme should be needed.

`-F` overrides the port if required; `--no-mc-ret` disables the session entirely - the always-on
unicast reply path (see above) keeps working exactly the same either way.

## RSI self-announcement

This periodically sends an RTCP RSI packet per channel over its MC RET session, advertising `-l` as the
unicast NACK target. Use `--rsi-interval` (default 5s) to define the interval, `--no-rsi` to disable.

## FCC channel resolution by dedicated port

Per DVB Annex I.2.7.2, a RAMS-R that can't include the media sender SSRC (the "ignore media SSRC"
TLV) needs to be resolved by which FCC server IP:port it arrived on, not by content. With
`--fcc-resolve-by-port`, every channel slot gets its own dedicated listen port -
`--fcc-resolve-base-port` (default `-l`'s port + 1) plus `hash(family,address,port) % -M` - bound
once at startup, independent of discovery order. RSI announces each channel's own port once known.
Off by default: this replaces SSRC-based dispatch for every channel, not just the ignore-SSRC case,
so it's an explicit opt-in. `dipisds` has matching `--fcc-resolve-by-port`/`--fcc-resolve-base-port`/
`--fcc-resolve-max-channels` flags to advertise the same ports via SD&S; RSI alone is sufficient
without it.

## Burst rate

`-X` sets the burst rate as a fixed multiple of each channel's own observed nominal bitrate, capped
by the client's optional Max Receive Bitrate TLV if that's lower. This is a fixed multiplier, not
an adaptive/congestion-aware ramp.

## Concurrency

One capture thread feeds the channel/ring/cache state (lock-free, single-writer).

`-w` worker threads (default: one per CPU core) each own an `SO_REUSEPORT` socket + epoll() loop on `-l`,
so incoming client requests are handled in parallel without a shared lock. This matters specifically
for a correlated loss event (e.g. electromagnetic interference across many subscribers at once)
hitting one channel's ring from many directions simultaneously.

A separate pacing thread (FCC only) ticks every active burst session on a fixed interval. The
burst-session table is guarded by a plain mutex rather than a lock-free design - claim/terminate/reap
are far less frequent than the per-tick send path, which never touches that lock.

## Privileges

Capture needs `CAP_NET_RAW`. Either grant it directly (`setcap cap_net_raw+ep` on the binary, or
systemd `AmbientCapabilities=CAP_NET_RAW`) and run as an unprivileged user, or start as root and
use `-u` to drop to an unprivileged user right after the capture handle opens.


## Stopping

`^C`, SIGINT or SIGTERM: stop the tool.


## Running under systemd

Since dipifccret only needs `CAP_NET_RAW`, not root, a unit granting just that capability
(instead of `-u`) is a reasonable starting point. See [dipifccret.service](dipifccret.service) for an example.


## Known gaps

* Single VLAN tag only (no QinQ).

## Examples

```sh
# edge box on a SPAN port mirroring the access switch, RET and FCC both on
dipifccret -g 239.0.0.0/8 -l 10.0.0.1:6000 -I eth0

# fixed worker count, privilege drop after opening the capture handle
dipifccret -g 239.0.0.0/8,224.1.2.0/24 -l 10.0.0.1:6000 -I eth0 -w 4 -u dipifccret

# RET only, no FCC
dipifccret -g 239.0.0.0/8 -l 10.0.0.1:6000 -I eth0 --no-fcc

# FCC only, tighter GOP-cache and burst-rate tuning
dipifccret -g 239.0.0.0/8 -l 10.0.0.1:6000 -I eth0 --no-ret -G 4000 -X 2.0
```
