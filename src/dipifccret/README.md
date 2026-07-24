# dipifccret

An RTP Retransmission (RET) and Server-based Fast Channel Change (FCC) **edge server**,
DVB-IPI Annex F (ETSI TS 102 034) and Annex I, built on IETF RFC 6285 (RAMS - Rapid
Acquisition of Multicast RTP Sessions).

Passively watches a mirrored/SPAN-ported multicast segment via libpcap, then keeps two
kinds of per-channel state from the same captured packets: a short seq-keyed ring for RET loss repair, 
and a Random Access Point (RAP)-anchored cache for FCC bursts. One capture thread and one `-l` listen socket 
serve both protocols - a client NACK always gets a direct unicast reply *and*, when the multicast RET session 
is enabled, is additionally repaired over that session (F.5.2); a RAMS-R request just before a channel join 
gets a burst of cached data at an accelerated rate so the client can start decoding immediately.

RET and FCC can each be run alone or together: `--no-ret` disables RET, `--no-fcc` disables FCC.

Small deployments may run `dipifccret` just once as a headend companion, larger ones might
want distributed deployments.

## Usage

```
dipifccret -g <range> -l <addr>:<port> [options]
```

## Options
```
  -g, --range <cidr>[,<cidr>...]   multicast range(s) to capture, IPv4 or IPv6
  -l, --listen <addr>:<port>       unicast bind, shared by RET and FCC traffic
  -I, --iface <iface>              capture interface (default: "any")
      --bpf <expr>                 raw BPF capture filter, overrides the -g auto-build
  -M, --max-channels <n>           preallocated channel slots (default: 0 = 384)
  -R, --rtx-pt <n>                 RTP payload type for retransmitted/burst packets (default: 99)
  -w, --workers <n>                -l socket worker threads (default: 0 = online CPU cores)
  -u, --user <user>                drop privileges to this user after opening the capture handle
  -v, --verbose                    periodic stats on stderr
      --color <when>               auto|always|never (default auto)
  -h, --help                       this help
```

### RET (Annex F)
```
      --no-ret                     disable RET entirely
  -B, --buffer <ms>                per-channel retransmission buffer depth (default: 2000)
  -F, --ff-port <port>             multicast RET session port (default: 0 = original channel's port)
      --no-mc-ret                  disable the multicast RET session, unicast-only repair
```

### FCC (Annex I)
```
      --no-fcc                     disable FCC entirely
  -G, --gop-cap <ms>               safety cap on cached GOP-in-progress duration (default: 8000)
  -C, --max-bursts <n>             preallocated concurrent burst-session slots (default: 4096)
  -X, --burst-multiplier <n>       burst rate as multiple of observed nominal bitrate (default: 1.5)
  -D, --burst-duration-cap <ms>    hard max burst duration regardless of signaling (default: 10000)
```

## Why passive capture, not an IGMP join

Joining every channel would mitigate all benefits of multicast distribution.
A mirror/SPAN port gives a read-only copy of exactly what's actually consumed, with zero footprint
on the distribution tree for the channels themselves.

`-g` is the authoritative multicast range whitelist (enforced in userspace regardless of the
actual capture filter). Channels are discovered dynamically within, not configured one by one.

`--bpf` can override the auto-built capture filter for mirror ports that need a `vlan` qualifier or other
trunk-specific handling. `-g` still applies afterwards either way.

## Multicast RET session

Per F.6.2.2, the repair session reuses the _same_ destination group:port as the original
channel. SSM already distinguishes it by source address, so no separate address scheme should be needed.

`-F` overrides the port if required; `--no-mc-ret` disables the session entirely - the always-on
unicast reply path (see above) keeps working exactly the same either way.

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

`^C`, SIGINT or SIGTERM - all worker threads, the pacing thread (if FCC is active) and the
capture loop shut down gracefully.

## Known gaps

* Single VLAN tag only (no QinQ); only Ethernet and Linux "any" (cooked) capture link types.
* RTX sequence numbers are shared across every RET send (MC and unicast, every channel and client)
  rather than a true per-session space per F.3.2.1 (one independent sequence per RET session).
* Self-detected upstream loss (dipifccret noticing a gap in its own capture, independent of any
  client NACK) will not trigger any RET repair. This is a server-only implementation, not a
  stacked client.
* RTX/burst packets use a fixed video-bearer DSCP class (F.9/I.2.12), not a byte-for-byte mirror
  of each original packet's own DSCP.
* RSI self-announcement (F.5.3) is not implemented. A client has to already know dipifccret's `-l`
  address by some other means - either dogmatically or by using SD&S RET/FCC records. `dipisds`
  supports this via `--ret-addr`/`--fcc-addr`.
* No transport-address-based FCC channel resolution: if a client's RAMS-R sets the "ignore media
  SSRC" TLV (asking the server to identify the channel by source address instead), the request is
  rejected (response 510) rather than resolved another way.
* Only RAMS-I response codes 200, 201, 400, 403 and 510 are implemented - the others RFC 6285
  defines (401, 402, 404, 504-506, 511, 100) aren't implemented.
* Only ever sends one RAMS-I per request (the initial accept/reject). RFC 6285's "RAMS-I update"
  mechanism, for changing burst parameters mid-flight, isn't implemented.
* RAMS-T always stops the burst immediately, ignoring the optional Extended RTP Seqnum TLV a
  client may include - no seqnum-based clipping of the tail end of the burst.

## Examples

```sh
# edge box on a SPAN port mirroring the access switch, RET and FCC both on
dipifccret -g 239.0.0.0/8 -l 10.0.0.1:6000

# fixed worker count, privilege drop after opening the capture handle
dipifccret -g 239.0.0.0/8,224.1.2.0/24 -l 10.0.0.1:6000 -w 4 -u dipifccret

# RET only, no FCC
dipifccret -g 239.0.0.0/8 -l 10.0.0.1:6000 --no-fcc

# FCC only, tighter GOP-cache and burst-rate tuning
dipifccret -g 239.0.0.0/8 -l 10.0.0.1:6000 --no-ret -G 4000 -X 2.0
```
