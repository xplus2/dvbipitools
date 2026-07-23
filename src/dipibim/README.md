# dipibim

A BiM (de-)compressor of plain TVA XML data ([dipixmltv](../dipixmltv/README.md)) encoding (ETSI TS 102 323#9.4).

Take it with a grain of salt, as ISO/IEC 15938-1:2002 is still paywalled.

```sh
dipibim -f xml [-i <path>] [-o <path>] [options]
dipibim -f bim [-i <path>] [-o <path>] [options]
```

## Input/output (`-i`, `-o`, `-f`)

* `-i`/`-o` are `-` for stdin/stdout (default). 
* `-f` names the input format, output is the other one.

No mapping table: dipibim doesn't touch xmltv ids or uris, it's just a wire-format conversion.

## Scope

Each `ProgramInformation`, `Schedule` (with its own `ScheduleEvent`s) and `ServiceInformation` is
its own separately BiM-encoded fragment (TS 102 822-3-2 4.3.1.1: elements below TVAMain's own
child tables "form fragments of their own"). 
No DVBSTP container, index structures or versioning (TS 102 822-3-2#4.5/4.6). 
See `dipiepg` for distribution.

## BiM output layout

`-f xml` output is TS 102 323 table 56/57's own DVBBiMAccessUnit shape: a count, then per fragment
a length + 16-bit `DVBContextPath` type tag + the fragment's own BiM bytes, wrapped as
`[4-byte BE][access unit][string repo]`. 
`dvbStringCodec` (9.4.3.3) points into that repo instead of inlining bytes, pooled across all fragments
(TS 102 822-3-2#4.8.4.1), fragment-order unchanged.

## Codecs (9.4.3)

All 5 except `dvbDurationCodec` (currently unused).
* `dvbStringCodec` - all string leaves
* `dvbLocatorCodec` - all anyURI leaves (CRIDs, service locators). String fallback only. Optimized-branch locators aren't supported.
* `dvbDateTimeCodec` - full-precision mode (8-byte MJD+ms), not the lossy 11-bit-minutes one. Always UTC.
* `dvbControlledTermCodec` - `Genre@href`; compact scheme+term branch only on an exact table 69
  match with an integer term, string fallback otherwise

## CRID caveat

CRIDs are derived from start time at write time, not stored.
A non-UTC source time round-trips to a different-looking CRID (same instant).

## Live stats (`-v`)

One line: channels and programmes converted.

## Examples

```sh
dipixmltv -f xmltv -M mapping.csv -i guide.xml -o guide.tva.xml
dipibim -f xml -i guide.tva.xml -o guide.bim

dipibim -f bim -i guide.bim -o guide.tva.xml
dipixmltv -f tva -i guide.tva.xml -o guide.xml
```
