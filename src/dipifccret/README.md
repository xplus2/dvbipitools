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
```
  -g, --range <cidr>[,<cidr>...]   multicast range(s) to capture, IPv4 or IPv6
  -l, --listen <addr>:<port>       unicast bind, shared by RET and FCC traffic
  -I, --iface <iface>              capture interface (required, single Ethernet NIC)
  -M, --max-channels <n>           preallocated channel slots (default: 0 = 384)
  -R, --rtx-pt <n>                 RTP payload type for retransmitted/burst packets (default: 99)
  -w, --workers <n>                -l socket worker threads (default: 0 = online CPU cores)
  -u, --user <user>                drop privileges to this user after opening the capture handle
  -v, --verbose                    periodic stats on stderr
      --color <when>               auto|always|never (default auto)
  -d, --daemonize                  fork to background after startup, detach from terminal
  -h, --help                       this help
```

### RET (Annex F)
```
      --no-ret                     disable RET entirely
  -B, --buffer <ms>                per-channel retransmission buffer depth (default: 2000)
  -F, --ff-port <port>             multicast RET session port (default: 0 = original channel's port)
      --no-mc-ret                  disable the multicast RET session, unicast-only repair
      --max-ret-clients <n>        pre-allocated unicast RTX per-client sequence slots,
                                   F.3.2.1 (default: 16384)
      --ret-client-idle-timeout <s> free a unicast RTX client slot after this many
                                   seconds with no NACKs (default: 300, 0 = never reap)
      --no-rsi                     disable RSI self-announcement
      --rsi-interval <s>           RSI self-announcement interval (default: 5 seconds)
```

### FCC (Annex I)
```
      --no-fcc                     disable FCC entirely
  -G, --gop-cap <ms>               safety cap on cached GOP-in-progress duration (default: 8000)
  -C, --max-bursts <n>             preallocated concurrent burst-session slots (default: 4096)
  -X, --burst-multiplier <n>       burst rate as multiple of observed nominal bitrate (default: 1.5)
  -D, --burst-duration-cap <ms>    hard max burst duration regardless of signaling (default: 10000)
      --max-buffer-fill-bound <ms> reject a RAMS-R Min RAMS Buffer Fill Requirement above this
                                   (default: 30000, 0 = none)
      --fcc-resolve-by-port        resolve ignore-media-ssrc RAMS-R by dedicated per-channel
                                   port instead of rejecting with 510 (default: off)
      --fcc-resolve-base-port <p>  base port for --fcc-resolve-by-port (default: 0 = -l port + 1)
      --congestion-nack-threshold <n>  NACKs during one burst before terminating it as
                                   congested (default: 5, 0 = disabled)
      --fcc-range <cidr>[,...]     restrict FCC to these -g sub-ranges (default: all of -g)
      --fcc-client-range <cidr>[,...] restrict FCC requests to these client source ranges
                                   (default: any client)
```

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
(instead of `-u`) is a reasonable starting point:

```ini
[Unit]
Description=dipifccret
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/dipifccret -g 239.19.0.0/16 -l 10.0.0.1:6000 -I eth0
Restart=on-failure
RestartSec=5
StartLimitIntervalSec=60
StartLimitBurst=10
DynamicUser=yes
AmbientCapabilities=CAP_NET_RAW
CapabilityBoundingSet=CAP_NET_RAW
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

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
