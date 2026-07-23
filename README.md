# dvbipitools

Command line tools for DVB-IPI streams.

## Tools

* [dipibim](src/dipibim/README.md) - convert between the plain TVA XML shape and its BiM binary encoding
* [dipiepg](src/dipiepg/README.md) - DVBSTP TVA EPG publisher / listener
* [dipiradiohead](src/dipiradiohead/README.md) - use Ice-/Shoutcast radio sources for multicast radio distribution
* [dipitvhead](src/dipitvhead/README.md) - use pre-encoded video streams as sources for multicast tv distribution
* [dipirec](src/dipirec/README.md) - record a DVB-IPI multicast
* [dipiscan](src/dipiscan/README.md) - scan an IP range for DVB-IPI multicasts
* [dipisds](src/dipisds/README.md) - DVBSTP/SD&S service discovery: announce/listen to a service list on multicast
* [dipixmltv](src/dipixmltv/README.md) - convert between XMLTV and the DVB-IPI TVA XML

## Build

Your choice. Go for the classics: `./configure --release && make` or use CMake:
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options: 
* `-DCMAKE_BUILD_TYPE=Debug|Release` / `--debug|--release`
* `-DDVBIPITOOLS_STATIC=ON` / `--static` for a static link
* `-DDIPIRADIOHEAD_TLS=OFF` / `-DDIPITVHEAD_TLS=OFF` to build either tool without HTTPS support
  (no OpenSSL needed); default is `ON` for both, auto-falls back to off if OpenSSL isn't found
  (same via the legacy Makefile, detected through `pkg-config`)

## Packaging
```sh
dpkg-buildpackage -b -us -uc
```
Build-Depends: `debhelper (>= 13)`, `cmake`, `libssl-dev` (`libssl-dev` is only needed for
dipiradiohead's and dipitvhead's HTTPS support)

## Location of deployment

| Tool          | Headend      | Edge | Client     | Reason                                       |
|---------------|--------------|------|------------|----------------------------------------------|
| dipiradiohead | ✔️           |      |            | Provides multicasts                          |
| dipitvhead    | ✔️           |      |            | Provides multicasts                          |
| dipisds       | `--announce` |      | `--listen` | Service accouncements / reader               |
| dipiepg       | `--announce` |      | `--listen` | EPG publisher / reader                       |
| dipixmltv     | (maybe)      |      | ✔️         | XMLTV converter (from/to)                    |
| dipifccret    | ✔️           | ✔️   |            | FCC/RET service, depends on network topology |
| dipirec       |              |      | ✔️         | Multicast recorder                           |
| dipiscan      |              |      | ✔️         | Scan for services (if no SD&S is in use)     |
| dipibim       |              |      | ✔️         | Debugging tool                               |

## Editorial notes

Some of the implemented formats are _not_ part of DVB-IPI. They exist for convenience and to bridge
between real-world usage of media formats and the standard.

### Notable additions
* dipiradiohead
  - Icecast/Shoutcast as an input source - none of this is part of DVB.
  - ICY `StreamTitle`, inline ID3v2 `TIT2`/`TPE1` mapping into EIT.
* dipitvhead
  - HbbTV AIT injection (`--hbbtv`, ETSI TS 102 809) is hybrid broadcast/broadband signalling,
    a separate spec from DVB-IPI itself.
* dipirec
  - `mkv` and `mka` containers.
  - `srt` subtitles from EBU Teletext (ETSI EN 300 706). SRT isn't a DVB format at all.
  - udpxy is not part of any DVB/ETSI specification.
* dipiscan
  - This tool, as a whole, would not be needed if deployments would cover DVBSTP/DS&S like `dipisds` does.
  - `m3u`, `xspf` and our own "lcoal" `csv` format.
* dipisds
  - Every format besides SD&S XML.
* dipixmltv
  - `xmltv` doesn't exist in the DVB world. TVA XML in BiM does.
  - CRIDs under TLD `crid://dipixmltv.invalid/...` - (RFC 2606) we're not a registered CRID authority.
* dipibim
  - Uncompressed representations of TVA EPG data are not specified in TS 102 539#7.2

### Known gaps
On the other hand, full DVB-IPI goes way beyond the scope of this toolkit.

* FEC & FCC/RET: Annex E, Annex F, Annex I/J
* RMS-FUS, Remote Management and Firmware Update
* DVB Companion Screens and Streams
* DVB Home Network, ETSI TS 102 905
* SD&S record types other than Broadcast Discovery / Service Provider Discovery (-5). `dipisds` only does those two.
  No CoD discovery, Package, Regionalisation or RMS-FUS discovery records.
* RTSP command/control for CoD services and multicast join (-6) - no CoD playback control here.
* DHCP-based IP address assignment for the HNED (-8).
* FUSS, the mandatory File Upload System Stub (-9)
* Content Download Services / CDS, push or pull (-10)
* QoS / DiffServ marking (-11)
* SRM delivery for Content Protection revocation (-12)
* Dynamic Service Management (-13)

## Licence

GPL-3.0-or-later. See `LICENSE` and `NOTICE`.
