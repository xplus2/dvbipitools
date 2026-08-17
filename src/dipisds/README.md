# dipisds

DVBSTP / SD&S (ETSI TS 102 034 clause 5, Service Discovery and Selection). Announce a service
list on multicast, or listen for one and write a playlist.

```
dipisds -a -i <path> -m <mcast>:<port> [options]
dipisds -l -m <mcast>:<port> [options]
```

## Options

| flag | long form         | argument                    | default                                   |
|------|-------------------|-----------------------------|--------------------------------------------|
| `-a` | `--announce`      |                             | headend mode: read `-i`, transmit on `-m`  |
| `-l` | `--listen`        |                             | client mode: receive on `-m`, write `-o`   |
| `-i` | `--input`         | `<path>`                    | announce: required                         |
| `-p` | `--provider`      | `<name>`                    | announce: required unless `-i` is `.xml`   |
| `-O` | `--offering`      | `<name>`                    | announce: required unless `-i` is `.xml`   |
| `-L` | `--lang`          | `<code>`                    | announce: `deu`                            |
| `-m` | `--mcast`         | `<g>:<p>`                   | required                                   |
| `-I` | `--iface`         | `<iface>`                   | kernel default                             |
| `-t` | `--interval`      | `<s>`                       | announce: `5`                              |
| `-t` | `--timeout`       | `<s>`                       | listen: `35`                               |
| `-o` | `--output`        | `<path>` / `-`              | listen: `-` (stdout)                       |
| `-f` | `--format`        | `m3u\|csv\|xspf\|xml\|null` | listen: from `-o` suffix                  |
| `-v` | `--verbose`       |                             | off                                        |
|      | `--color`         | `auto\|always\|never`       | `auto`                                     |
|      | `--ret-addr`      | `<addr>:<port>`             | announce: off (no RET advertised)          |
|      | `--ret-rtx-time`  | `<ms>`                      | announce: `2000`                           |
|      | `--ret-rtx-pt`    | `<n>`                       | announce: `99`                             |
|      | `--ret-mc`        |                             | announce: off (unicast RET only)           |
|      | `--ret-mc-port`   | `<port>`                    | announce: each service's own port          |
|      | `--fcc-addr`      | `<addr>:<port>`             | announce: off (no FCC advertised)          |
|      | `--fcc-rtx-time`  | `<ms>`                      | announce: `2000`                           |
|      | `--fcc-rtx-pt`    | `<n>`                       | announce: `99`                             |
|      | `--metrics`       | `<path>`                    | announce: `/run/dvbipitools/metrics.sock`  |
|      | `--metrics-id`    | `<name>`                    | announce: none (metrics disabled unless set) |
|      | `--metrics-interval` | `<s>`                    | announce: `5`                              |
|      | `--packages`      | `<path>`                    | announce: off (no Package Discovery)       |
|      | `--cells`         | `<path>`                    | announce: off (no Regionalisation Discovery) |
|      | `--rms-name`      | `<name>`                    | announce: off (no RMS-FUS Discovery)       |
|      | `--rms-lang`      | `<code>`                    | announce: `deu`                            |
|      | `--rms-location`  | `<uri>`                     | announce: required with `--rms-name`       |
|      | `--rms-logo`      | `<uri>`                     | announce: off                              |
|      | `--fus-name`      | `<name>`                    | announce: off (no RMS-FUS Discovery)       |
|      | `--fus-lang`      | `<code>`                    | announce: `deu`                            |
|      | `--fus-id`        | `<n>`                       | announce: required with `--fus-name`       |
|      | `--fus-announce`  | `<addr>:<port>`             | announce: off                              |
|      | `--fus-logo`      | `<uri>`                     | announce: off                              |
| `-d` | `--daemonize`     |                             | off (foreground)                           |
| `-h` | `--help`          |                             |                                             |

## Announce (`-a`)

Reads `-i` and transmits it as DVBSTP-carried SD&S records on `-m`, repeating every `-t`
(`--interval`) seconds, default 5. ETSI TS 102 034 caps the full announce cycle at 30 seconds;
keep `-t` comfortably under that. SIGHUP re-reads `-i` (see [Signals](#signals)).

`-i` format is taken from the path suffix:

| suffix  | content                                                                             |
|---------|-------------------------------------------------------------------------------------|
| `.csv`  | `name,uri[,tsid,onid,sid]`, same as dipiscan's own csv output                       |
| `.m3u`  | `#EXTINF` with optional `tsid=".." onid=".." sid=".."` attributes, same as dipiscan |
| `.xspf` | XSPF, triplet in a `<extension application="urn:dvbipitools:dvb-triplet">`          |
| `.xml`  | a hand-authored SD&S document, or dipiscan's own `-f xml` output                    |

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
          <IPMulticastAddress Address="239.19.75.1" Port="8700" Streaming="rtp"/>
        </ServiceLocation>
        <TextualIdentifier ServiceName="channel1"/>
        <DVBTriplet OrigNetId="1" TSId="1" ServiceId="1"/>
      </SingleService>
    </ServiceList>
  </BroadcastDiscovery>
</ServiceDiscovery>
```

## Advertising dipifccret RET service (`--ret-addr`)

Off by default. `--ret-addr <addr>:<port>` (see `dipifccret -l`) adds an
`RTPRetransmission` record (ETSI TS 102 034#5.2.12.26, `RETInfoType`) to every service in
the Broadcast Discovery.

> Note: `.xml` passthrough is an exception. The provided XML will not get changed.

### Example
```xml
<IPMulticastAddress Address="239.19.75.1" Port="8700" Streaming="rtp">
  <RTPRetransmission>
    <RTCPReporting DestinationAddress="10.0.0.1" DestinationPort="6000"/>
    <UnicastRET rtx-time="2000" RTPPayloadTypeNumber="99"/>
    <MulticastRET GroupAddress="239.19.75.1" DestinationPort="8700" rtx-time="2000" RTPPayloadTypeNumber="99"/>
  </RTPRetransmission>
</IPMulticastAddress>
```

### Parameters

- `--ret-rtx-time <ms>` / `--ret-rtx-pt <n>` should be the same as dipifccret's `-B`/`-R` values (or defaults). requires `--ret-addr`.
- `--ret-mc` adds `MulticastRET`, like running dipifccret _without_ `--no-mc-ret`;
  its group:port always match this service's own `IPMulticastAddress`, per Annex F.6.2.2's
  same-group:port reuse. `--ret-mc-port` overrides the port, see `dipifccret -F`.
- Left out on purpose, each because the schema's own "if absent" meaning already matches
  dipifccret's actual behaviour: 
  + RET `ssrc` is unknown until dipifccret sees live packets
  + unicast `DestinationPort`/`SourcePort` because `dipifccret` replies from the same socket the NACK arrived on
  + `SourceAddress` on both RET types: CoD/trick-mode only
  + `RTSPControlURL`, RTCP RR fields


## Advertising dipifccret FCC/RAMS service (`--fcc-addr`)

Off by default. `--fcc-addr <addr>:<port>` (see `dipifccret -l`) adds a
`ServerBasedEnhancementServiceInfo` record (ETSI TS 102 034 Annex I.2.14,
`ServerBasedEnhancementServiceInfoType`) to every service in the Broadcast Discovery. This is a
separate, newer element from `--ret-addr`'s `RTPRetransmission` - both can be given together, and
per the spec an HNED using both services is meant to use only this element, ignoring
`RTPRetransmission`.

> Note: `.xml` passthrough is an exception. The provided XML will not get changed.

### Example
```xml
<IPMulticastAddress Address="239.19.75.1" Port="8700" Streaming="rtp">
  <ServerBasedEnhancementServiceInfo>
    <EnhancementService>FCC</EnhancementService>
    <RTCPReporting DestinationAddress="10.0.0.1" DestinationPort="6000"/>
    <Retransmission_session DestinationPort="6000" rtx-time="2000" RTPPayloadTypeNumber="99"/>
  </ServerBasedEnhancementServiceInfo>
</IPMulticastAddress>
```

### Parameters

- `--fcc-rtx-time <ms>` requires `--fcc-addr`. No corresponding dipifccret flag - this is the
  schema's own `Retransmission_session` session-depth attribute, independent of dipifccret's own
  burst-rate/duration tuning.
- `--fcc-rtx-pt <n>` requires `--fcc-addr`, should match dipifccret's `-R` value (or default).
- `EnhancementService` is always `"FCC"`, never a combined value - the XSD only defines two
  enumerated values (`"FCC"`, `"RET"`) and the element occurs at most once; per Annex I.2.3 the
  FCC and RET server coincide, so `"FCC"` already implies combined capability when both are
  offered by the same server.
- Left out on purpose, matching `--ret-addr`'s own scope: `SourcePort`, `rtcp-mux`,
  `DestinationPort-ForRTCPReporting`, `trr-int`, `RTSPControlURL`, and the RTCPReporting-side
  `rtcp-bandwidth`/`rtcp-rsize`/`dvb-*` attributes.


## Package Discovery (`--packages`)

Off by default. `--packages <path>` (announce, requires a `.csv`/`.m3u`/`.xspf` `-i`, same
restriction as `--ret-addr`/`--fcc-addr`) adds a Package Discovery record (payload 0x05, ETSI
TS 102 034 clause 5.2.13.4, `PackagedServices`).

One line per package:
```
id,name,lang,visible,svc1|svc2|svc3
```
`id` is the numeric `Id`. `name` and `lang` set the `PackageName` text and its language (ISO 639-2).
`visible` is `0` or `1` (default=1). 
Service names are matched against `-i` entries by `ServiceName`. Matches get a `DVBTriplet`, unmatched names are emitted without one.

Known gaps: 
`PackageDescription` (BCG linkage), `LogicalChannelNumber`, `PackageReference`, `PackageAvailability`, `URILinkage`,
`ciAncillaryData`, deprecated `CountryAvailability`.

## Regionalisation Discovery (`--cells`)

Off by default. `--cells <path>` (announce, same `-i` restriction as `--packages`) adds a
Regionalisation Discovery record (payload 0x07, clause 5.2.13.8, `RegionalisationOffering`).

One line per cell:
```
id,country,type:value,type:value,...
```
`id` is the `Cell`'s `Id`, matched against `ServiceAvailability`/`PackageAvailability` `Cells`
elsewhere. `country` is the 2-letter ISO 3166 `CountryCode`. The `type:value` pairs are RFC 4676 civic address types,
nested outer to inner in the order given.

Only a single outer-to-inner chain per cell, not a general branching `CA` tree.

## RMS-FUS Discovery (`--rms-name` / `--fus-name`)

Off by default. Adds an RMS-FUS Discovery record (payload 0x08, clause 5.2.13.6, `RMSFUSDiscoveryType`), 
same `-i` restriction as `--packages`. `--rms-name`/`--fus-name` are mutually exclusive:
the XSD models this record as a choice, RMS provider or FUS provider, never both.

`--rms-name <name>` (requires `--rms-location <uri>`):
```xml
<RMSProvider RMSLocation="https://rms.example/">
  <RMSName Language="deu">My RMS</RMSName>
</RMSProvider>
```
`--fus-name <name>` (requires `--fus-id <n>`, optional `--fus-announce <addr>:<port>`):
```xml
<FUSProvider>
  <FUSName Language="deu">My FUS</FUSName>
  <FUSID>1</FUSID>
  <FUSAnnouncement>
    <MulticastAnnouncementAddress Address="239.1.1.1" Port="5000"/>
  </FUSAnnouncement>
</FUSProvider>
```
`--rms-lang` and `--fus-lang` set the display name's language, and `--rms-logo` and `--fus-logo` set the optional `LogoURI`.

Known gaps: 
`DSMProvider`, `RMSID`, `Description`, and `FUSAnnouncement`'s `FUSUnicastAnnouncement`/`QRCLocation`.


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

## Signals

* `^C`, SIGINT or SIGTERM: stop the process
* SIGHUP: re-read `-i` from disk. On error, the previous input keeps being announced and the error
  is logged. (No effect in `-l` mode).

## Scope

DVBSTP framing is generic; on announce, the structured (non-`.xml`) side understands Service
Provider Discovery, Broadcast Discovery, Package Discovery, Regionalisation Discovery, and
RMS-FUS Discovery (payloads 0x01/0x02/0x05/0x07/0x08), each a minimal producer subset of its
ETSI TS 102 034 clause 5.2.13 type. Anything wider goes through `.xml` passthrough (see
Announce, above). Listen (`-l`) remains Broadcast Discovery only.

It is not a general SD&S client: CoD and BCG-carriage records (BCG has its own tool, `dipibcg`)
remain out of scope. Compression is always "none": gzip is spec-restricted to other payload ids,
and BiM is not implemented.

## Running under systemd

If you want dipisds announcing continuously under systemd, the unit below is a reasonable
starting point for `-a` mode; `-l` mode works the same way, swapped for `-l`/`-o` and a
writable path instead of `ReadOnlyPaths`.

```ini
[Unit]
Description=dipisds
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/dipisds -a -i /etc/dipisds/channels.csv -p example.org -O "My Headend" -m 239.255.0.1:3937
Restart=on-failure
RestartSec=5
StartLimitIntervalSec=60
StartLimitBurst=10
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadOnlyPaths=/etc/dipisds

[Install]
WantedBy=multi-user.target
```

## Examples

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
