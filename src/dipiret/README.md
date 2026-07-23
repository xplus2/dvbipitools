# dipiret

A RTP Retransmission (RET) **edge server**, DVB-IPI Annex F (ETSI TS 102 034). 

Passively watches a mirrored/SPAN-ported multicast segment via libpcap, then buffers recent
packets per channel. Every client NACK always gets a direct unicast reply (F.3.1/Figure F.2's
mandatory baseline) *and*, when the multicast RET session is enabled, is additionally repaired
over that session (F.5.2) so one send covers every affected viewer - both, not either/or.

```
dipiret -g <range> -l <addr>:<port> [options]
```


## Why passive capture, not an IGMP join

At the edge, multicast already flowing on a segment *is* the demand signal. IGMP-snooping switches only replicate 
to ports with an active join. Joining every channel itself would force full-catalog replication onto dipiret's 
switch port regardless of real viewership. You could use broadcasts instead of multicasts then.

A mirror/SPAN port gives a read-only copy of exactly what's already flowing, with zero footprint
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


## Concurrency

One capture thread feeds the channel/ring state (lock-free, single-writer design - see comments
in `channel.c`). 

`-w` worker threads (default: one per CPU core) each own an `SO_REUSEPORT` socket + epoll() loop on `-l`, 
so incoming client NACKs are handled in parallel without a shared lock. 
This matters specifically for a correlated loss event (e.g. electromagnetic interference across many
subscribers at once) hitting one channel's ring from many directions simultaneously.


## Privileges

Capture needs `CAP_NET_RAW`. Either grant it directly (`setcap cap_net_raw+ep` on the binary, or
systemd `AmbientCapabilities=CAP_NET_RAW`) and run as an unprivileged user, or start as root and
use `-u` to drop to an unprivileged user right after the capture handle opens.


## Stopping

`^C`, SIGINT or SIGTERM - all worker threads and the capture loop shut down gracefully.


## Known gaps

* Single VLAN tag only (no QinQ); only Ethernet and Linux "any" (cooked) capture link types.
* RTX sequence numbers are shared across every send (MC and unicast, every channel and client)
  rather than a true per-session space per F.3.2.1 (one independent sequence per RET session).
* Self-detected upstream loss (dipiret noticing a gap in its own capture, independent of any client NACK)
  will not trigger any repair. This is a server-only implementation, not a stacked client.
* RTX packets use a fixed video-bearer DSCP class (F.9), not a byte-for-byte mirror of each original packet's own DSCP.
* RSI self-announcement (F.5.3) is not implemented. A client has to already know dipiret's `-l`
  address by some other means. Either dogmatically or by using SD&S RET records. 
  `dipisds` supports this by using the optional `--ret-*` args.


## Examples

```sh
# edge box on a SPAN port mirroring the access switch, one worker per core
dipiret -g 239.0.0.0/8 -l 10.0.0.1:6000

# fixed worker count, privilege drop after opening the capture handle
dipiret -g 239.0.0.0/8,224.1.2.0/24 -l 10.0.0.1:6000 -w 4 -u dipiret
```
