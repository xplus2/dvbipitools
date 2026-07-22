# dipixmltv

Converts between [xmltv](https://github.com/XMLTV/xmltv) and the DVB-IPI EPG XML shape (TVA metadata, ETSI TS 102 822-3-1/-3-2), plain, not BiM encoded

```sh
dipixmltv -f xmltv -M <map> [-i <path>] [-o <path>] [options]
dipixmltv -f tva [-R <revmap>] [-i <path>] [-o <path>] [options]
dipixmltv -S <scan.csv> [-i <guide.xml>] [-o <path>] [options]
```

## Input/output (`-i`, `-o`, `-f`)

`-i`/`-o` are `-` for stdin/stdout (the default for both). `-f` names the **input** format; the output is always the other one:

* `-f xmltv`: read xmltv, write TVA-shaped xml
* `-f tva`: read TVA-shaped xml, write xmltv

## Channel mapping
xmltv channel `id` values usually carry no DVB identity, and dipiscan-scanned `tsid`/`onid`/`sid` triplets are commonly degenerate in real deployments.
It's entirely normal for every service on a headend to report the same `tsid=1 onid=0 sid=1`.
The multicast `uri` is the one thing actually guaranteed unique per service, so that's what dipixmltv keys on.

`-M` (`--map`, required for `-f xmltv`): `id,uri,tsid,onid,sid` per line, the same shape as dipiscan's own csv, with column 1 repurposed to the xmltv channel id.
An xmltv channel with no entry here is silently dropped, along with its programmes.

`-R` (`--reverse-map`, optional, `-f tva` only): `uri,id` per line. When converting TVA back to xmltv, an incoming `serviceId` might not be ours to begin with 
(like a real broadcast decoded by dipibim). This renames it to your preferred local xmltv id, looked up by the service's multicast uri. 
Without `-R`, the incoming `serviceId` is kept as-is.

## Building the mapping (`-S`)
```sh
dipixmltv -S scan.csv -i guide.xml -o mapping.csv
```

Matches xmltv channels (by every `<display-name>`, not just the first) against a dipiscan csv capture, by name:

* Exact match (case-insensitive) -> written as a live mapping line.
* No exact match, but something close -> written `#`-commented with the closest guess, for you to confirm or fix.
* Nothing close at all -> `# UNMATCHED: <id> (<name>)`.

Review the output before using it as `-M` - this is a starting point, not a guarantee.

## xmltv conformance

This tool's output aims to be DTD-valid XMLTV (`display-name`/`title` are required non-empty elements in the real `xmltv.dtd`; 
falls back to the channel id / "(untitled)" if the source data doesn't have one). 

The reader is lenient. Real-world XMLTV often sits at the edge of the DTD or beyond it, and conforming files should always work.
Non-conforming quirks that don't affect the fields we need aren't rejected either.

## CRIDs

TVA's `programId`/`crid` reference is a constrained `anyURI` (`crid://.../...`), so raw XMLTV ids (which can contain anything)
aren't valid there directly. `dipixmltv` encodes the xmltv channel id instead of using the DVB triplet - the triplet's real-world degeneracy would
make CRIDs collide across channels; 

The XMLTV ID is guaranteed unique within its own source:

```
crid://dipixmltv.invalid/<percent-encoded-channel-id>/<programme-start-timestamp>
```

> The problem:
> Having `programme-start-timestamp` as a part of the ID means that further down the chain, if you base recordings (days in advance) 
> on this ID, but the programme gets rescheduled, you're lost.
> 
> Conclusion:
> Don't use this ID as a hard reference if you're implementing a PVR.

## Live stats (`-v`)

One line: channels and programmes read.

## Some examples

```sh
# build a mapping from a real scan + your guide, then review it
dipiscan -f csv -o scan.csv
dipixmltv -S scan.csv -i guide.xml -o mapping.csv
# ... edit mapping.csv, uncomment/fix what -S wasn't sure about ...

# xmltv -> tva
dipixmltv -f xmltv -M mapping.csv -i guide.xml -o guide.tva.xml

# tva -> xmltv, renaming foreign serviceIds back to your own ids
dipixmltv -f tva -R revmap.csv -i received.tva.xml -o guide.xml
```
