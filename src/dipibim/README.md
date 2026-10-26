# dipibim

A generic XML to BiM converter. Schemas are loaded from XSD files at runtime, no schema is compiled in.
Converts the plain TVA XML of [dipixmltv](../dipixmltv/README.md) to its BiM encoding (ETSI TS 102 323 §9.4) and back.

This implementation follows [ISO/IEC 15938-1:2002](https://www.iso.org/standard/34228.html) and
[ISO/IEC 23001-1:2006](https://www.iso.org/standard/35417.html),
with the profiles of 
[ETSI TS 102 323](https://www.etsi.org/deliver/etsi_ts/102300_102399/102323/01.04.01_60/ts_102323v010401p.pdf) §9.4,
[ETSI TS 102 822-3-2](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220302/01.06.01_60/ts_1028220302v010601p.pdf) §4.4 and
[ETSI TS 102 034](https://www.etsi.org/deliver/etsi_ts/102000_102099/102034/01.04.01_60/ts_102034v010401p.pdf) §5.5.

## Usage
```sh
dipibim -f xml -s <schema> [-i <path>] [-o <path>] [options]
dipibim -f bim -s <schema> [-i <path>] [-o <path>] [options]
```

## Options

| flag | long form         | argument                      | default                         |
|------|-------------------|-------------------------------|---------------------------------|
| `-i` | `--input`         | `<path>`                      | stdin                           |
| `-o` | `--output`        | `<path>`                      | stdout                          |
| `-f` | `--format`        | `xml\|bim`                    | required                        |
| `-s` | `--schema`        | `<path>`                      | required, repeatable            |
|      | `--profile`       | `dvb-tva\|tva\|sdns\|generic` | `dvb-tva`                       |
|      | `--syntax`        | `23001-1\|15938-1`            | `23001-1`, generic              |
|      | `--compat`        | `<name>[,<name>...]`          | none                            |
|      | `--len-mode`      | `0\|1\|2`                     | `0`, generic                    |
|      | `--deferred`      |                               | off, generic                    |
|      | `--cast`          |                               | off, generic                    |
|      | `--dvb-locator`   |                               | off, dvb-tva                    |
|      | `--dvb-duration`  |                               | off, dvb-tva                    |
|      | `--dvb-pubtime`   |                               | off, dvb-tva                    |
|      | `--zbuf-size`     | `<bytes>`                     | `0` (1000), tva and sdns        |
|      | `--zgather`       |                               | off, tva and sdns               |
| `-v` | `--verbose`       |                               | off                             |
|      | `--color`         | `auto\|always\|never`         | `auto`                          |
| `-c` | `--config`        | `<path>`                      | `/etc/dvbipitools/dipibim.yaml` |
|      | `--config-strict` |                               | config file issues are errors   |
|      | `--configtest`    |                               | check the config file           |
| `-h` | `--help`          |                               |                                 |

## Input/output (`-i`, `-o`, `-f`)

* `-i`/`-o` are `-` for stdin/stdout (default).
* `-f` names the input format, output is the other one.

No mapping table: dipibim doesn't touch xmltv ids or uris, it's just a wire-format conversion.

## Scope

Profiles `dvb-tva` and `tva`: a `TVAMain` document is split into the fragment types of TS 102 822-3-2 table 7. A fragment is the ultimate
atomic part of a description that can be transmitted independently (TS 102 822-3-2 4.3): the TVAMain fragment holds the root element
and the tables below it (4.3.1.1), every other type (`ProgramInformation`, `Schedule` with its `ScheduleEvent`s, `ServiceInformation`, ...)
is its own separately BiM-encoded fragment. `sdns` writes one `ServiceDiscovery` segment, `generic` one topmost element.
No DVBSTP container, index structures or versioning (TS 102 822-3-2 4.5/4.6). See [dipibcg](../dipibcg/README.md) for distribution.

## Configuration file

All options (except `-h`, `-c`, `--config-strict` and `--configtest`) can be set in a YAML file, see [dipibim.yaml](dipibim.yaml).
Debian installs config examples to: `/usr/share/dvbipitools/etc/`.

## Schemas (`-s`)

Schema files and directories, any number of each. A directory is searched recursively for the `.xsd` declaring the namespace of the
input root element (`-f xml`) or of `SchemaURI[0]` in the stream (`-f bim`). Imports and includes resolve in the directories
and next to the schema files. One schema file is used as is. Of several the one declaring that namespace is used. Two files for
one namespace are an error, name the file with `-s`.

A stream whose DecoderInit lists further schemas (`SchemaURI[1..]`, TS 102 323 9.4.4) is decoded with every one of them found in
the `-s` files and directories, by namespace (`dvb-tva`, `tva`, `generic`). The chunks of a schema that is not found are skipped
and `[BW000770]` names it.

The encoders list `SchemaURI[0]` and the namespaces of the document (elements, attributes, `xsi:type` values) that the main schema
does not define, each found in the `-s` files and directories. Content of those schemas is coded as chunks of them. A namespace
without schema file stays with the main schema, as wildcard content, and is not listed.

## Required XSDs

No schema is shipped. Download the archives of the namespaces you use, unpack them into a directory and point `-s` at it.
Imports (the MPEG-7 schemas, `tva2_metadata_3-3.xsd` of the extended namespace from V1.9.1 on, the XML namespace schema) are found
in the same directories. The namespace of a document selects its row.

TV-Anytime (ETSI TS 102 822-3-1), profiles `dvb-tva` and `tva`:

| TS 102 822-3-1  | Namespace               | Main XSD                    | Archive                                                                                                                            | SHA-256 |
|-----------------|-------------------------|-----------------------------|------------------------------------------------------------------------------------------------------------------------------------|---------|
| V1.1.1          | `urn:tva:metadata:2002` | `tva_metadata_v13.xsd`      | [ts_1028220301v010101p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.01.01_60/ts_1028220301v010101p0.zip) | `8714a26f06498fdf32463ed0ad09ae802c43c519b1ea80df6ec3853fb9c44cb7` |
| V1.2.1          | `urn:tva:metadata:2004` | `tva_metadata_v13_am1.xsd`  | [ts_1028220301v010201p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.02.01_60/ts_1028220301v010201p0.zip) | `e1d87834979d28607bb6c341cfe886139c3e485159cf85cd35331388f66963fb` |
| V1.3.1          | `urn:tva:metadata:2005` | `tva_metadata_3-1_v131.xsd` | [ts_1028220301v010301p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.03.01_60/ts_1028220301v010301p0.zip) | `360fc5352a78ae690211111b5a8107851c8fb317366145c800e03cd6d044a2ea` |
| V1.4.1          | `urn:tva:metadata:2007` | `tva_metadata_3-1_v141.xsd` | [ts_1028220301v010401p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.04.01_60/ts_1028220301v010401p0.zip) | `69bc21795f1de389029c914249cc0b4bbfad8ddffc2b2e350f85df63dd063f74` |
| V1.5.1          | `urn:tva:metadata:2008` | `tva_metadata_3-1_v151.xsd` | [ts_1028220301v010501p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.05.01_60/ts_1028220301v010501p0.zip) | `137755234d9002f49b7df4e38c7e22f42e9ef779d07e1261cb60b3fee70cd0e0` |
| V1.6.1          | `urn:tva:metadata:2010` | `tva_metadata_3-1_v161.xsd` | [ts_1028220301v010601p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.06.01_60/ts_1028220301v010601p0.zip) | `f0e4cffe520d3a4cdb40bd8ab3d62983f05a52f6df8110b039ab1235fa781754` |
| V1.7.1          | `urn:tva:metadata:2011` | `tva_metadata_3-1_v171.xsd` | [ts_1028220301v010701p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.07.01_60/ts_1028220301v010701p0.zip) | `227087aec48ea314802261b18c9d6f84343a1f172f7e13a0d5520046e6e06202` |
| V1.8.1          | `urn:tva:metadata:2012` | `tva_metadata_3-1_v181.xsd` | [ts_1028220301v010801p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.08.01_60/ts_1028220301v010801p0.zip) | `b7e6888f9abdcfe0b0fd1799686f014adf4eee6fdeafc4064cb85115faec6ed5` |
| V1.9.1          | `urn:tva:metadata:2015` | `tva_metadata_3-1.xsd`      | [ts_1028220301v010901p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.09.01_60/ts_1028220301v010901p0.zip) | `79d26a00beef1d143c688febac1870c4960c91c703fda669921f76ea2af2d789` |
| V1.9.2          | `urn:tva:metadata:2015` | `tva_metadata_3-1.xsd`      | [ts_1028220301v010902p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.09.02_60/ts_1028220301v010902p0.zip) | `78cc740956f8dae903499e0e7596cabe0fd673721ab19176a456e03e5d0df25c` |
| V1.10.1         | `urn:tva:metadata:2017` | `tva_metadata_3-1.xsd`      | [ts_1028220301v011001p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.10.01_60/ts_1028220301v011001p0.zip) | `aa699ec756bfcdf5f49fd25a639b9bdf4b7e5eb2240cac0543bb2b42edd73ebc` |
| V1.11.1         | `urn:tva:metadata:2019` | `tva_metadata_3-1.xsd`      | [ts_1028220301v011101p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.11.01_60/ts_1028220301v011101p0.zip) | `a1600524518f8921ada307a390a9ae3a656ff852ca6058be075c1bbddb28e8ff` |
| V1.11.2         | `urn:tva:metadata:2019` | `tva_metadata_3-1.xsd`      | [ts_1028220301v011102p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.11.02_60/ts_1028220301v011102p0.zip) | `244f784d452115df3b7af4e26094fa8a46e749f3094d8a5aa4e91ef5cbc448ff` |
| V1.12.1         | `urn:tva:metadata:2023` | `tva_metadata_3-1.xsd`      | [ts_1028220301v011201p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.12.01_60/ts_1028220301v011201p0.zip) | `a5cfba8e7f858021231400897fd57367a37e39fb1475ed847462d70eef8d95a7` |
| V1.13.1         | `urn:tva:metadata:2024` | `tva_metadata_3-1.xsd`      | [ts_1028220301v011301p0.zip](https://www.etsi.org/deliver/etsi_ts/102800_102899/1028220301/01.13.01_60/ts_1028220301v011301p0.zip) | `2fb3df64f66c12f2e94c16ae5e6ae208ac54cb63371205242dd66a68faa6a658` |

DVB-IPI Service Discovery and Selection (ETSI TS 102 034), profile `sdns`:

| TS 102 034 | Archive                                                                                                                | SHA-256                                                            |
|------------|------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------|
| V1.1.1     | [ts_102034v010101p0.zip](https://www.etsi.org/deliver/etsi_ts/102000_102099/102034/01.01.01_60/ts_102034v010101p0.zip) | `29dd959c80aa3d88a339194f917896ef327b30041453a0eca6d351a83990f04c` |
| V1.3.1     | [ts_102034v010301p0.zip](https://www.etsi.org/deliver/etsi_ts/102000_102099/102034/01.03.01_60/ts_102034v010301p0.zip) | `d43bccd42bcf1dd7469f4da1aad95f347e1d58c48b638ef1c399dd0ec3f712ec` |
| V1.5.1     | [ts_102034v010501p0.zip](https://www.etsi.org/deliver/etsi_ts/102000_102099/102034/01.05.01_60/ts_102034v010501p0.zip) | `0688120effc7461e19b2583592c40b1520743994b43029f63421cb2819bea7c4` |
| V2.1.1     | [ts_102034v020101p0.zip](https://www.etsi.org/deliver/etsi_ts/102000_102099/102034/02.01.01_60/ts_102034v020101p0.zip) | `84e81ba891405818f908eca1bd19e4c0aa9fa5e4f373b42105817e24071d2c31` |

## Profiles

| profile   | stream                                                                   | codecs                                         |
|-----------|--------------------------------------------------------------------------|------------------------------------------------|
| `dvb-tva` | DVB-TVA-init, `[4-byte BE][DVBBiMAccessUnit][string repository]`         | the DVB codecs of TS 102 323 9.4.3             |
| `tva`     | TVA-init, BiMAccessUnit (TS 102 822-3-2 4.4.2), no string repository     | Zlib strings, tvadateTime, tvadate             |
| `sdns`    | DVB-TVA-init, one `ServiceDiscovery` segment, context path `0x0030`      | Zlib strings                                   |
| `generic` | DecoderInit (ISO/IEC 23001-1, `--syntax 15938-1`: ISO/IEC 15938-1:2002)  | none, see `--len-mode`, `--deferred`, `--cast` |

`dvb-tva` and `tva` split a `TVAMain` document into fragments (TS 102 822-3-2 table 7) and join them again on decode.
The TVAMain fragment is a copy without the nested fragments, the containers stay.
`dvb-tva` refuses documents that break the content rules of TS 102 323 clause 8: references between fragments (8.2), CRIDs and Synopsis
length (8.3, 8.4), Schedules and BroadcastEvents (8.5, 8.8), the DVB locator of a ServiceInformation (8.6), `mediaTimeUnit` (8.7.3) and the
publisher format (8.9), with `[BE400318]`. The other profiles apply no clause 8 rules. dipibim writes what it is given, it drops and rewrites nothing.

`tva` and `sdns` need zlib at build time, without it they refuse with `[BE400317]`.

## MPEG-7 Systems conformance streams (`--syntax`, `--compat`)

Without `--compat` the coding is as ISO/IEC 15938-1:2002 or ISO/IEC 23001-1 specify. The 169 single access unit bitstreams of the MPEG-7 Systems conformance suite (ISO/IEC
15938-7, `BiM/singleAU`, decoded against the MPEG-7 2001 schema) deviate from it in the points below. `--compat` takes a comma separated
list of them, `conformance` stands for all. With `--profile generic --syntax 15938-1 --compat conformance` all 169 streams decode to the
same elements, attributes and values as the reference decoder's XML. The input is the stream's `decoderInit.bin` without its last byte,
followed by the access unit file.

| `--compat`       | the streams                                                                                                                            | ISO/IEC 15938-1:2002, 23001-1                                                                           |
|------------------|----------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| `no-schema-mode` | no SchemaModeUpdate in elements although the DecoderInit lists three schemas                                                           | 15938-1:2002 8.4.1, 23001-1 7.4.1: present when NumberOfSchemas > 1                                     |
| `time-flags`     | time points and durations (basicTimePointType, timePointType, mediaTimePointType, basicDurationType, durationType, mediaDurationType)  | 15938-1:2002 8.5.4.2.5, 8.5.4.2.6: every derived type, omitted sections flagged 0, month 1-12, day 1-31 |
| `attr-local`     | attributes ordered by local name                                                                                                       | 15938-1:2002 8.5.3: expanded name                                                                       |
| `fixed-attr`     | optional attributes with a fixed value are coded                                                                                       | 15938-1:2002 8.5.3: fixed attributes are suppressed                                                     |
| `list-string`    | IDREFS, NMTOKENS and ENTITIES are strings                                                                                              | 15938-1:2002 8.5.4.2.3: lists                                                                           |

The multiple access unit streams (`BiM/multipleAUs`) and the fragment reference, mixed content and Zlib sets of the Amd 1 suite are not
decoded: they need the InitialDescription of the DecoderInit and the type codecs it names.

## BiM output layout

`dvb-tva` output starts with the DVB-TVA-init message (TS 102 323 table 53) with the DecoderInit naming the namespace, then
TS 102 323 table 56/57's DVBBiMAccessUnit: a count, then per fragment a length + 16-bit `DVBContextPath` type tag + the fragment's
own BiM bytes, wrapped as `[4-byte BE][access unit][string repository]`. `-f bim` input needs that init prefix. The other profiles
are laid out in the table above.

## Codecs (`dvb-tva`)

The five default codecs of TS 102 323 9.4.3:
* `dvbStringCodec` - all string leaves
* `dvbLocatorCodec` - anyURI leaves, written as string, optimized form with `--dvb-locator`
* `dvbDateTimeCodec` - dateTime leaves, PublishedTime form with `--dvb-pubtime`
* `dvbControlledTermCodec` - `termReferenceType`
* `dvbDurationCodec` - duration leaves, written as string, minutes with `--dvb-duration`

## Live stats (`-v`)

One line: fragment count and output size when encoding, fragment count when decoding.

## Specification conformance

Five specifications define the BiM coding: ISO/IEC 15938-1:2002 (1st edition), ISO/IEC 23001-1 (2nd edition), ETSI TS 102 822-3-2
(TV-Anytime), ETSI TS 102 323 (DVB-TVA) and ETSI TS 102 034 5.5 (SD&S).

The default is profile for this tool is `dvb-tva`: TS 102 323 with the ISO/IEC
15938-1:2002 payload syntax. Without `--compat` every profile codes as the specifications in the table below require, nothing is relaxed.

| `--profile`         | specification                                             | payload syntax          |
|---------------------|-----------------------------------------------------------|-------------------------|
| `dvb-tva` (default) | ETSI TS 102 323 clause 9.4, content rules of clause 8     | ISO/IEC 15938-1:2002    |
| `tva`               | ETSI TS 102 822-3-2 clause 4                              | ISO/IEC 15938-1:2002    |
| `sdns`              | ETSI TS 102 034 5.5                                       | ISO/IEC 23001-1         |
| `generic`           | ISO/IEC 23001-1, `--syntax 15938-1`: ISO/IEC 15938-1:2002 | per `--syntax`          |

MPEG-7 Systems conformance streams (ISO/IEC 15938-7, `BiM/singleAU` like the reference decoder):
```
dipibim -f bim -s <MPEG-7 2001 schema> --profile generic --syntax 15938-1 --compat conformance -i <decoderInit.bin minus last byte + access unit>
```

Compliance modes:

| mode                            | set with                                      | effect                                                                     |
|---------------------------------|-----------------------------------------------|----------------------------------------------------------------------------|
| default                         | -                                             | specifications of the profile, table above                                 |
| `--syntax 23001-1\|15938-1`     | `--syntax`, `syntax:`, `generic` only         | payload syntax, default 23001-1                                            |
| MPEG-7 conformance streams      | `--compat conformance`, `compat:`             | all deviations below, with `--profile generic --syntax 15938-1`            |
| `--compat no-schema-mode`       | `--compat`, `compat:`                         | no SchemaModeUpdate with several schemas                                   |
| `--compat time-flags`           | `--compat`, `compat:`                         | time point and duration flags of the reference software                    |
| `--compat attr-local`           | `--compat`, `compat:`                         | attributes ordered by local name                                           |
| `--compat fixed-attr`           | `--compat`, `compat:`                         | optional fixed attributes coded                                            |
| `--compat list-string`          | `--compat`, `compat:`                         | IDREFS, NMTOKENS, ENTITIES as strings                                      |

`--compat` takes a comma separated list. Details of each deviation: [MPEG-7 Systems conformance streams](#mpeg-7-systems-conformance-streams---syntax---compat).

## Examples

```sh
dipixmltv -f xmltv -M mapping.csv -i guide.xml -o guide.tva.xml
dipibim -f xml -s schemas/ -i guide.tva.xml -o guide.bim

dipibim -f bim -s schemas/ -i guide.bim -o guide.tva.xml
dipixmltv -f tva -i guide.tva.xml -o guide.xml

dipibim -f xml -s schemas/ --profile sdns -i sds.xml -o sds.bim
```
