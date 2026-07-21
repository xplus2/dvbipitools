# dipisds

DVBSTP / SD&S (ETSI TS 102 034 clause 5, Service Discovery and Selection). Announce a service
list on multicast, or listen for one and write a playlist.

```
dipisds -a -i <path> -m <mcast>:<port> [options]
dipisds -l -m <mcast>:<port> [options]
```

## Announce (`-a`)

Reads `-i` and transmits it as DVBSTP-carried SD&S records on `-m`, repeating every `-t`
(`--interval`) seconds, default 5. ETSI TS 102 034 caps the full announce cycle at 30 seconds;
keep `-t` comfortably under that.

`-i` format is taken from the path suffix:

| suffix  | content                                                                          |
|---------|-----------------------------------------------------------------------------------|
| `.csv`  | `name,uri[,tsid,onid,sid]`, same as dipiscan's own csv output                     |
| `.m3u`  | `#EXTINF` with optional `tsid=".." onid=".." sid=".."` attributes, same as dipiscan |
| `.xspf` | XSPF, triplet in a `<extension application="urn:dvbipitools:dvb-triplet">`        |
| `.xml`  | a hand-authored SD&S document, or dipiscan's own `-f xml` output - sent as-is (payload id read from its root element) |

`uri` is `rtp://[@]<addr>:<port>` or `udp://[@]<addr>:<port>`, matching dipirec/dipiscan's own
convention. Missing `tsid`/`onid` default to 1, missing `sid` auto-increments from 1.

For `.csv`/`.m3u`/`.xspf`, `-p` (`--provider`, the `DomainName`) and `-O` (`--offering`, the
provider's display `Name`) are required. `-L` (`--lang`, a 3-letter ISO 639-2 code for that
display name) defaults to `deu`. dipisds builds both a Broadcast Discovery record (payload
0x02, the service list) and a Service Provider Discovery record (payload 0x01, self-pointing
at the same `-m` socket) - a listener demuxes the two by DVBSTP payload id, not by address, so
one multicast group is enough.

`.xml` input is sent exactly as given, under whichever payload id its root element implies; no
Service Provider record gets synthesized for it. This is what dipisds itself generates from a
`.csv`/`.m3u`/`.xspf` source - a minimal Service Provider Discovery record (payload 0x01):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ServiceDiscovery xmlns="urn:dvb:metadata:iptv:sdns:2008-1">
  <ServiceProviderDiscovery>
    <ServiceProvider DomainName="example.org" Version="1">
      <Name Language="deu">My Headend</Name>
      <Offering>
        <Push Address="239.255.0.1" Port="3937"><PayloadId Id="2"/></Push>
      </Offering>
    </ServiceProvider>
  </ServiceProviderDiscovery>
</ServiceDiscovery>
```

and a minimal Broadcast Discovery record (payload 0x02):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ServiceDiscovery xmlns="urn:dvb:metadata:iptv:sdns:2008-1">
  <BroadcastDiscovery DomainName="example.org" Version="1">
    <ServiceList>
      <SingleService>
        <ServiceLocation>
          <IPMulticastAddress Address="239.2.24.1" Port="8208" Streaming="rtp"/>
        </ServiceLocation>
        <TextualIdentifier ServiceName="channel1"/>
        <DVBTriplet OrigNetId="1" TSId="1" ServiceId="1"/>
      </SingleService>
    </ServiceList>
  </BroadcastDiscovery>
</ServiceDiscovery>
```

## Listen (`-l`)

Joins `-m`, reassembles DVBSTP segments, and after `-t` (`--timeout`) seconds (default 35, just
over the spec's 30s max cycle time so one full cycle is always captured) writes whatever
Broadcast Discovery records it saw as a playlist.

`-f` (`--format`) is `m3u`/`csv`/`xspf` (same shapes as dipiscan, including the triplet), `xml`
(dumps the reassembled document as received, for debugging), or `null`.

## Network interface (`-I`)

Picks the interface for the multicast join/send. Without it, the kernel's default multicast
route is used.

## Live stats (`-v`)

Announce: one line per cycle. Listen: one line per segment received.

## Stopping

`^C`, SIGINT or SIGTERM.

## Scope

DVBSTP framing is generic; the XML side only understands the subset of the SD&S schema this
tool itself produces (`ServiceProviderDiscovery`/`BroadcastDiscovery`, `SingleService` with
`ServiceLocation`/`TextualIdentifier`/`DVBTriplet`). It is not a general SD&S client - other
record types (CoD, packages, BCG, regionalisation) are out of scope. Compression is always
"none": gzip is spec-restricted to other payload ids, and BiM is not implemented.

## Some examples

```sh
# announce dipiscan's own scan output
dipiscan -f csv -o channels.csv
dipisds -a -i channels.csv -p example.org -O "My Headend" -m 239.255.0.1:3937

# listen and write an m3u
dipisds -l -m 239.255.0.1:3937 -o discovered.m3u

# dipiscan's own SD&S xml output, or a hand-authored document, sent as-is
dipiscan -f xml -P example.org -o scan.xml
dipisds -a -i scan.xml -m 239.255.0.1:3937
```
