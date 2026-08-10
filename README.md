# dvbipitools

Tools for handling DVB-IPI streams.

They kept piling up in private VCS over many years, so I finally decided to polish, consolidate, and publish them.
GitHub is not the primary development repository, but releases are published here and pull requests are welcome.

AI helped with the README files, `-h` boilerplate, and test scaffolding they never had before.
I did not let it mess up the hard parts.


## Fantastic tools and where to deploy them

| Tool                                              | Headend | Edge | Client | Lab | Purpose                                           |
|---------------------------------------------------|---------|------|--------|-----|---------------------------------------------------|
| [dipitvhead](src/dipitvhead/README.md)            | ✔️      |      |        | ✔️  | Provide IPTV multicasts                           |
| [dipiradiohead](src/dipiradiohead/README.md)      | ✔️      |      |        | ✔️  | Provide radio multicasts                          |
| [dipimetrics](src/dipimetrics/README.md) | ✔️      |      |        | ✔️  | Per-host OpenMetrics                              |
| [dipifccret](src/dipifccret/README.md)            | ✔️      | ✔️   |        | ✔️  | RAMS-based FCC (Annex I) and RET (Annex F) server |
| [dipisds](src/dipisds/README.md)                  | ✔️      |      | ✔️     | ✔️  | DVBSTP/SD&S service discovery (announce & listen) |
| [dipibcg](src/dipibcg/README.md)                  | ✔️      |      | ✔️     | ✔️  | DVBSTP/TVA BCG (publisher & reader)               |
| [dipixmltv](src/dipixmltv/README.md)              | ✔️      |      | ✔️     | ✔️  | XMLTV to/from DVB-IPI TVA XML converter           |
| [dipirec](src/dipirec/README.md)                  |         |      | ✔️     | ✔️  | Multicast to file/stdout recorder                 |
| [dipiscan](src/dipiscan/README.md)                |         |      | ✔️     | ✔️  | Scan for multicast TV/radio services (w/o SD&S)   |
| [dipibim](src/dipibim/README.md)                  |         |      |        | ✔️  | TVA XML BiM encoder/decoder (to debug `dipibcg`)  |
| [dipicam378](src/dipicam378/README.md)            |         |      |        | ✔️  | cs378x CAS test smartcard emulator                |
| [dipidescramble](src/dipidescramble/README.md)    |         |      |        | ✔️  | CAS validation client / descrambler               |


## Build

Your choice. Go for the classics: `./configure --release && make -j"$(nproc)"` or use CMake:
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Options
| CMake                                                                    | configure               |                                                      |
|--------------------------------------------------------------------------|-------------------------|------------------------------------------------------|
| `-DCMAKE_BUILD_TYPE=Debug\|Release`                                      | `--debug` / `--release` | Build type                                           |
| `-DDVBIPITOOLS_STATIC=ON`                                                | `--static`              | Static linking                                       |
| `-DDIPIRADIOHEAD_TLS=OFF` / `-DDIPITVHEAD_TLS=OFF` / `-DDIPIREC_TLS=OFF` | `--no-tls`              | Build the respective tool without TLS source support |
| `-DDIPITVHEAD_CSA2=OFF` / `-DDIPIRADIOHEAD_CSA2=OFF` / `-DDIPIDESCRAMBLE_CSA2=OFF` | `--no-csa2`   | Build the respective tool without CSA2               | 

> Note: The build automatically disables TLS support if OpenSSL is not found.


### Dependencies

* **libssl-dev**
  + Required for HTTPS sources in `dipitvhead` and `dipiradiohead`.
  + Enables _CISSA/AES-128_ Conditional Access in `dipitvhead` and `dipiradiohead`.
  + Required to build `dipicam378`/`dipidescramble` at all - RSA/AES crypto is their
    whole purpose, so both are skipped entirely if not found.
* **libdvbcsa-dev**
  + Optional; required only if you need CSA support in `dipitvhead`, `dipiradiohead`
    or `dipidescramble`.

## Packaging
```sh
dpkg-buildpackage -b -us -uc
```

Build-Depends: `debhelper (>= 13)`, `cmake`, `libssl-dev`.


## Editorial notes

Some of the implemented formats are _not_ part of DVB-IPI. They exist for convenience and to bridge
between real-world usage of media formats and the standard.


### Notable additions
* dipitvhead
  - HbbTV AIT injection (`--hbbtv`, ETSI TS 102 809) is hybrid broadcast/broadband signaling, a separate spec.
  - MPTS support is more like an interpretation than "specs driven".
    ETSI TS 102 034 v2.1.1 mentions Multi Program Transport Streams (MPTS) exactly twice:
    + In the abbreviation list
    + According to §7.1.0, Content Service Providers can use them
  - BISS (EBU Tech 3292). DVB-IPI prioritizes SimulCrypt.
* dipiradiohead
  - Icecast/Shoutcast as an input source - none of this is part of DVB.
  - ICY `StreamTitle`, inline ID3v2 `TIT2`/`TPE1` mapping into EIT.
* dipirec
  - `mkv` and `mka` containers.
  - `srt` subtitles from EBU Teletext (ETSI EN 300 706). SRT isn't a DVB format.
  - udpxy is not part of any DVB/ETSI specification.
* dipiscan
  - This tool would largely be unnecessary if deployments consistently used DVBSTP/SD&S as implemented by `dipisds`.
  - `m3u`, `xspf` and our own "local" `csv` format.
* dipisds
  - All supported formats other than SD&S XML/BiM are convenience additions.
* dipixmltv
  - XMLTV is not part of DVB-IPI; TVA XML encoded as BiM is.
  - CRIDs using the authority `dipixmltv.invalid`, reserved under RFC 2606 because this project is not a registered CRID authority.
* dipibim
  - An uncompressed representation of TVA EPG data is not specified by ETSI TS 102 539, §7.2.
* dipimetrics
  - OpenMetrics is not part of any DVB/ETSI specification.


### Known gaps
On the other hand, full DVB-IPI goes way beyond the scope of this toolkit.

* FEC (Annex E) 
* Companion Stream FCC (Annex J). RAMS-based FCC is provided by `dipifccret`.
* RMS-FUS, Remote Management and Firmware Update
* DVB Companion Screens and Streams
* DVB Home Network, ETSI TS 102 905
* SD&S record types other than Broadcast Discovery / Service Provider Discovery (§5). `dipisds` only does those two.
  No CoD discovery, Package, Regionalisation or RMS-FUS discovery records
* RTSP command/control for CoD services and multicast join (§6)
* DHCP-based IP address assignment for the HNED (§8)
* FUSS, the mandatory File Upload System Stub (§9)
* Content Download Services / CDS, push or pull (§10)
* Full QoS/DiffServ behavior (§11): Only limited or fixed DSCP handling is implemented
* SRM delivery for Content Protection revocation (§12)
* Dynamic Service Management (§13)

Indirectly related: 

* `dipitvhead`, `dipiradiohead` and `dipidescramble` don't support ETSI TS 103 197 CSA3/CSA-ALT

## Licence

GPL-3.0-or-later. See `LICENSE` and `NOTICE`.
