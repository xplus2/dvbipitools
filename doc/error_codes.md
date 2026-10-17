# Error codes

Lists like this one can easily fall out of sync.
Don't be surprised if an error code you encounter isn't covered here. 

## Format

`<tool><type><number>`, for example `TE890001`.
In outputs, error codes are always wrapped in `[` and `]`.  

### Tools

| Char | Tool                    |
|------|-------------------------|
| T    | dipitvhead              |
| R    | dipiradiohead           |
| V    | dipirist                |
| H    | dipisrt                 |
| M    | dipimetrics             |
| F    | dipifccret              |
| A    | dipisds                 |
| G    | dipibcg                 |
| X    | dipixmltv               |
| C    | dipirec                 |
| S    | dipiscan                |
| Y    | dipixy                  |
| B    | dipibim                 |
| 3    | dipicam378              |
| D    | dipidescramble          |
| I    | dvbipitools (multicall) |
| L    | shared libs             |

### Types

| Char | Meaning               |
|------|-----------------------|
| S    | startup error         |
| A    | startup warning       |
| W    | runtime warning       |
| E    | runtime error         |
| D    | startup/runtime debug |

### Number ranges

| Range         | Category                        |
|---------------|---------------------------------|
| 000000-099999 | input                           |
| 100000-199999 | output                          |
| 200000-299999 | error (meta)                    |
| 300000-399999 | authentication and cryptography |
| 400000-499999 | format                          |
| 500000-599999 | system                          |
| 600000-699999 | remote                          |
| 700000-799999 | misc                            |
| 800000-999999 | reserved                        |

Config file problems can be of type `A` or type `S`, depending on `--config-strict`.

## Code list

### 0xxxxx Input

| Number  | Codes             | Message                                                                                        |
|---------|-------------------|------------------------------------------------------------------------------------------------|
| 000001  | MS                | tls cert given without tls key                                                                 |
| 000002  | MS                | tls key given without tls cert                                                                 |
| 000100  | AE,BS,GE,XS       | cannot open %s                                                                                 |
| 000101  | GW                | reload failed, keeping previous guide                                                          |
| 000102  | GS                | invalid -w window hours: %s                                                                    |
| 000103  | AS,GS,RS,TS       | invalid -m group:port: %s                                                                      |
| 000104  | AS,GS             | invalid -t seconds: %s                                                                         |
| 000105  | *S                | invalid --color: %s (auto\|always\|never)                                                      |
| 000106  | AS,GS,RS,TS,VS    | invalid --dscp: %s (video-high\|video-low\|voice\|signalling\|best-effort\|0..63)              |
| 000107  | *S                | unexpected argument: %s                                                                        |
| 000108  | AS,GS             | exactly one of -a/--announce or -l/--listen is required                                        |
| 000109  | AS,GS             | missing -m multicast group:port                                                                |
| 000110  | *S                | missing -i input                                                                               |
| 000111  | GS                | missing -M map                                                                                 |
| 000112  | AS,GS             | --metrics-id is announce-only                                                                  |
| 000113  | GS                | -Z/--compress is announce-only                                                                 |
| 000114  | AA,GA             | neither announce nor listen set (required unless given on the command line)                    |
| 000115  | AA,GA             | mcast not set (required unless given on the command line)                                      |
| 000116  | AA,GA             | input not set (required for announce unless given on the command line)                         |
| 000117  | GA                | map not set (required for announce unless given on the command line)                           |
| 000118  | AA,GA             | announce and listen are mutually exclusive                                                     |
| 000119  | GA                | metrics.id, compress and refresh are announce-only                                             |
| 000120  | *A                | metrics.sock and metrics.interval require metrics.id                                           |
| 000121  | BS                | invalid -f format: %s (xml\|bim)                                                               |
| 000122  | BS                | missing -f format (xml\|bim)                                                                   |
| 000123  | BE                | read failed                                                                                    |
| 000124  | AS,GS             | invalid -t interval: must be >= 1                                                              |
| 000125  | 3S                | invalid -p port: %s                                                                            |
| 000126  | 3S                | invalid --caid: %s                                                                             |
| 000127  | 3S                | invalid --algo: %s (cissa\|csa2)                                                               |
| 000128  | 3S                | missing -k device key                                                                          |
| 000129  | 3A                | key not set (required unless given on the command line)                                        |
| 000130  | 3S                | missing -s device serial                                                                       |
| 000131  | 3W                | caid %04X not supported, sending CMD08 for srvid=%04X                                          |
| 000132  | 3W                | unknown command %d, len=%zu, bytes=%s(slot %d)                                                 |
| 000133  | 3S                | bind address %s: %s                                                                            |
| 000134  | MS                | invalid --auth: %s                                                                             |
| 000135  | FS,MS             | invalid -l addr:port: %s                                                                       |
| 000136  | MS                | invalid -e expiry seconds: %s                                                                  |
| 000137  | 3A                | serial not set (required unless given on the command line)                                     |
| 000138  | MS                | invalid listen address %s                                                                      |
| 000139  | MS                | socket path too long: %s                                                                       |
| 000140  | LE                | rist: invalid peer url: %s                                                                     |
| 000141  | CS,DS,HS,VS       | missing -o output                                                                              |
| 000142  | VS                | exactly one of -i/-o must be rist://, the other a plain endpoint                               |
| 000143  | VS                | --secret requires --profile main                                                               |
| 000144  | VS                | --encryption-type requires --profile main                                                      |
| 000145  | CS,HS,RS,TS,VS    | --al-fec requires --al-fec-port                                                                |
| 000146  | HA,RA,TA,VA       | --al-fec-port has no effect without --al-fec                                                   |
| 000147  | VA                | --al-fec has no effect, non-rist:// side isn't rtp://                                          |
| 000148  | HA,VS             | --insecure needs -i https://                                                                   |
| 000149  | HS,VS             | invalid -i: %s                                                                                 |
| 000150  | HS,VS             | invalid -o: %s                                                                                 |
| 000151  | VS                | invalid --profile: %s (simple\|main)                                                           |
| 000152  | VS                | invalid --encryption-type: %s (128\|256)                                                       |
| 000153  | VS                | invalid --buffer: %s (1..60000 ms)                                                             |
| 000154  | CS,HS,RS,TS,VS,YS | invalid --al-fec: %s (want L:D, L*D<=400, L<=40)                                               |
| 000155  | CS,HS,RS,TS,VS    | invalid --al-fec-port: %s                                                                      |
| 000156  | CA,HA,VA          | in not set (required unless given on the command line)                                         |
| 000157  | CA,HA,VA          | out not set (required unless given on the command line)                                        |
| 000158  | VA                | exactly one of in/out must be rist://, the other a plain endpoint                              |
| 000159  | VA                | secret requires profile main                                                                   |
| 000160  | VA                | encryption-type requires profile main                                                          |
| 000161  | *A                | metrics.inspect-ts requires metrics.id                                                         |
| 000162  | CA,HA,RA,VA       | al-fec requires al-fec-port                                                                    |
| 000163  | HA,RA,VA          | al-fec-port has no effect without al-fec                                                       |
| 000164  | SS                | invalid -m address: %s (addr, addr/prefixlen, or startaddr-stopaddr)                           |
| 000165  | SS                | invalid -p port range: %s                                                                      |
| 000166  | SS                | invalid -f format: %s (m3u\|csv\|xspf\|xml\|null)                                              |
| 000167  | SS                | invalid -t timeout: %s (1..3600 seconds)                                                       |
| 000168  | SS                | invalid -j jets: %s (1..%d)                                                                    |
| 000169  | SS                | invalid -u http-proxy address: %s                                                              |
| 000170  | SS                | invalid -x path template: %s (%%g, %%p, %%%% only)                                             |
| 000171  | SS                | missing -P provider (required for -f xml)                                                      |
| 000172  | SS                | --http-path needs -u/--http-proxy                                                              |
| 000173  | SA                | format xml requires provider (unless given on the command line)                                |
| 000174  | SA                | http-path needs http-proxy                                                                     |
| 000175  | AW                | reload failed, keeping previous input                                                          |
| 000176  | AS                | missing -p provider (required unless -i is .xml)                                               |
| 000177  | AS                | missing -O offering (required unless -i is .xml)                                               |
| 000178  | AS                | --ret-addr has no effect with a raw .xml -i input                                              |
| 000179  | AS                | --ret-rtx-time/--ret-rtx-pt/--ret-mc/--ret-mc-port/--ret-rsi-mc-ret require --ret-addr         |
| 000180  | AS                | --ret-rsi-mc-ret requires --ret-mc                                                             |
| 000181  | AS                | --fcc-addr has no effect with a raw .xml -i input                                              |
| 000182  | AS                | --fcc-rtx-time/--fcc-rtx-pt/--fcc-resolve-* require --fcc-addr                                 |
| 000183  | AS                | --al-fec-addr has no effect with a raw .xml -i input                                           |
| 000184  | AS                | --al-fec-pt requires --al-fec-addr                                                             |
| 000185  | AS                | --packages/--cells/--rms-name/--fus-name have no effect with a raw .xml -i input               |
| 000186  | AS                | --rms-name and --fus-name are mutually exclusive                                               |
| 000187  | AS                | --rms-lang/--rms-location/--rms-logo require --rms-name                                        |
| 000188  | AS                | --rms-name requires --rms-location                                                             |
| 000189  | AS                | --fus-lang/--fus-id/--fus-announce/--fus-logo require --fus-name                               |
| 000190  | AS                | --fus-name requires --fus-id                                                                   |
| 000191  | AS                | --ret-* options are announce-only                                                              |
| 000192  | AS                | --fcc-* options are announce-only                                                              |
| 000193  | AS                | --packages/--cells/--rms-*/--fus-* options are announce-only                                   |
| 000194  | AS                | invalid -L lang: %s (3-letter ISO 639-2 code)                                                  |
| 000195  | AS                | invalid --format: %s (m3u\|csv\|xspf\|xml\|null)                                               |
| 000196  | AS                | invalid --ret-addr: %s                                                                         |
| 000197  | AS                | invalid --ret-rtx-time: %s                                                                     |
| 000198  | AS                | invalid --ret-rtx-pt: %s (0..127)                                                              |
| 000199  | AS,CS             | invalid --ret-mc-port: %s                                                                      |
| 000200  | AS                | invalid --fcc-addr: %s                                                                         |
| 000201  | AS                | invalid --al-fec-addr: %s                                                                      |
| 000202  | AS                | invalid --al-fec-pt: %s (0..127)                                                               |
| 000203  | AS                | invalid --fcc-rtx-time: %s                                                                     |
| 000204  | AS                | invalid --fcc-rtx-pt: %s (0..127)                                                              |
| 000205  | AS,FS             | invalid --fcc-resolve-base-port: %s                                                            |
| 000206  | AS                | invalid --fcc-resolve-max-channels: %s                                                         |
| 000207  | AS                | invalid --rms-lang: %s (3-letter ISO 639-2 code)                                               |
| 000208  | AS                | invalid --fus-lang: %s (3-letter ISO 639-2 code)                                               |
| 000209  | AS                | invalid --fus-id: %s                                                                           |
| 000210  | AS                | invalid --fus-announce: %s                                                                     |
| 000211  | AA                | provider not set                                                                               |
| 000212  | AA                | offering not set                                                                               |
| 000213  | AA                | ret, fcc, al-fec, metrics id, packages, cells, rms and fus are announce-only                   |
| 000214  | AA                | rms and fus are mutually exclusive                                                             |
| 000215  | AA                | rms.name requires rms.location                                                                 |
| 000216  | AA                | rms.lang, rms.location and rms.logo require rms.name                                           |
| 000217  | AA                | fus.name requires fus.id                                                                       |
| 000218  | AA                | fus.lang, fus.id, fus.announce and fus.logo require fus.name                                   |
| 000219  | AA                | ret.* settings require ret.addr                                                                |
| 000220  | AA                | ret.rsi-mc-ret requires ret.mc                                                                 |
| 000221  | AA                | fcc.* settings require fcc.addr                                                                |
| 000222  | AA                | al-fec.pt requires al-fec.addr                                                                 |
| 000223  | AA                | ret, fcc, al-fec, packages, cells, rms and fus have no effect with a raw .xml input            |
| 000224  | AE                | line %d: expected name,uri                                                                     |
| 000225  | AE                | too many entries (max %d)                                                                      |
| 000226  | AE                | xspf track missing <location>                                                                  |
| 000227  | AE                | cannot read %s                                                                                 |
| 000228  | AE                | %s: too many packages (max %d)                                                                 |
| 000229  | AE                | %s: line %d: too many services in package (max %d)                                             |
| 000230  | AE                | %s: too many cells (max %d)                                                                    |
| 000231  | AE                | %s: line %d: too many CA entries (max %d)                                                      |
| 000232  | HS                | exactly one of -i/-o must be srt://, the other a plain endpoint                                |
| 000233  | HS                | bonding several srt:// peers requires --group-mode                                             |
| 000234  | HS                | --group-mode has no effect with a single srt:// peer                                           |
| 000235  | HS                | --rendezvous is not combinable with srt://@ (listener)                                         |
| 000236  | HS                | --rendezvous is not combinable with --group-mode                                               |
| 000237  | HS                | --rendezvous requires --local <host:port>                                                      |
| 000238  | HS                | --passphrase must be 10..79 characters                                                         |
| 000239  | HS                | --pbkeylen requires --passphrase                                                               |
| 000240  | HA                | --al-fec has no effect, the non-srt:// side isn't rtp://                                       |
| 000241  | HS                | --group-mode needs a libsrt built with bonding support (ENABLE_BONDING=ON)                     |
| 000242  | HS                | invalid --group-mode: %s (broadcast\|backup)                                                   |
| 000243  | HS                | invalid --local: %s                                                                            |
| 000244  | HS                | invalid --pbkeylen: %s (16\|24\|32)                                                            |
| 000245  | HS                | invalid --latency: %s (1..60000 ms)                                                            |
| 000246  | HS                | invalid --send-buffer-mult: %s (1..32)                                                         |
| 000247  | HA                | exactly one of in/out must be srt://, the other a plain endpoint                               |
| 000248  | HA                | bonding several srt:// peers requires group-mode                                               |
| 000249  | HA                | group-mode has no effect with a single srt:// peer                                             |
| 000250  | HA                | rendezvous is not combinable with srt://@ (listener)                                           |
| 000251  | HA                | rendezvous is not combinable with group-mode                                                   |
| 000252  | HA                | rendezvous requires local                                                                      |
| 000253  | HA                | passphrase must be 10..79 characters                                                           |
| 000254  | HA                | pbkeylen requires passphrase                                                                   |
| 000255  | XS                | invalid -f format: %s (xmltv\|tva)                                                             |
| 000256  | XS                | missing -f format (xmltv\|tva)                                                                 |
| 000257  | XS                | missing -M map (required for -f xmltv)                                                         |
| 000258  | XD                | %d channels, %d programmes read                                                                |
| 000259  | XS                | -%c not allowed with -f %s                                                                     |
| 000260  | XS                | -%c not allowed with -S                                                                        |
| 000261  | XS                | cannot open reverse map file %s                                                                |
| 000262  | IS                | dvbipitools: %s: not a directory                                                               |
| 000263  | IS                | dvbipitools: path too long: %s/%s                                                              |
| 000264  | IS                | usage: %s --list                                                                               |
| 000265  | IS                | usage: %s --install [-h] DIR                                                                   |
| 000266  | IS                | %s: no applet named '%s'                                                                       |
| 000267  | IS                | %s: not invoked under a known name                                                             |
| 000268  | LS                | --biss2-sw is mutually exclusive with --cas-algo/--cas-ecmg                                    |
| 000269  | LS                | --biss1-sw is mutually exclusive with --cas-algo/--cas-ecmg                                    |
| 000270  | LS                | --biss2-ca-receivers is mutually exclusive with --cas-algo/--cas-ecmg                          |
| 000271  | LS                | --biss2-sw and --biss1-sw are mutually exclusive                                               |
| 000272  | LS                | --biss2-ca-receivers is mutually exclusive with --biss2-sw/--biss1-sw                          |
| 000273  | LS                | --biss2-emit-esw requires --biss2-sw                                                           |
| 000274  | LS                | --biss2-ca-session-id requires --biss2-ca-receivers                                            |
| 000275  | LS                | --biss2-ca-receivers needs --cas-cp-duration >= 1000 (Tech 3292-s1 T_ECM_change_min)           |
| 000276  | LS                | --cas-algo requires --cas-ecmg                                                                 |
| 000277  | LS                | --cas-ecmg %s:%u requires --cas-super-id                                                       |
| 000278  | LS                | --cas-ecmg %s:%u requires --cas-ecm-id                                                         |
| 000279  | LS                | --cas-ecm-pid and --cas-emm-pid must differ (--cas-ecmg %s:%u)                                 |
| 000280  | LS                | --cas-emmg-reverse is mutually exclusive with --cas-emmg-listen (--cas-ecmg %s:%u)             |
| 000281  | LS                | --cas-emmg-reverse is mutually exclusive with --cas-emmg-max-conns (--cas-ecmg %s:%u)          |
| 000282  | LS                | --cas-ecmg %s:%u: invalid --cas-cwenc-* configuration                                          |
| 000283  | LS                | --cas-ecm-pid/--cas-emm-pid collide across --cas-ecmg vendors                                  |
| 000284  | LS                | --cas-emmg-listen port %u used by more than one --cas-ecmg vendor                              |
| 000285  | LS                | cas_group: invalid configuration                                                               |
| 000286  | LS                | cw_encryption: aes_mode=ecb is invalid with cw_len=8 (AES has no 8-byte block mode)            |
| 000287  | LS                | cw_encryption: no fixed_key configured and no key list loaded                                  |
| 000288  | LS                | cw_encryption: cannot open key list '%s'                                                       |
| 000289  | LS                | cw_encryption: key list '%s' must be exactly %d bytes                                          |
| 000290  | LS                | cw_encryption: invalid algorithm '%s' (des56\|aes128\|aes256)                                  |
| 000291  | LS                | cw_encryption: invalid aes_mode '%s' (stream\|ecb)                                             |
| 000292  | LS                | cw_encryption: fixed_key must be %zu hex chars for %s                                          |
| 000293  | LS                | emmg: listen address %s: %s                                                                    |
| 000294  | LA                | unknown key '%s'                                                                               |
| 000295  | LA,LS             | key '%s' is not a list                                                                         |
| 000296  | LA                | duplicate key '%s', last one wins                                                              |
| 000298  | LS                | invalid value for '%s': %s                                                                     |
| 000299  | LA,LS             | key '%s': cannot read file '%s': %s                                                            |
| 000301  | LS                | invalid list item in '%s': %s                                                                  |
| 000302  | LS                | cannot open config: %s                                                                         |
| 000303  | LS                | not a regular file                                                                             |
| 000304  | LW                | playlist: skipping track, location longer than %zu bytes                                       |
| 000305  | LW                | playlist: title truncated to %zu bytes                                                         |
| 000306  | LW                | input %u (%s): HLS media sequence reset, rejoining                                             |
| 000307  | LW                | input %u (%s): HLS segment gap, skipping ahead                                                 |
| 000308  | LD                | dropped stale snapshot for %s/%s (seq %llu)                                                    |
| 000309  | LE                | mpts: %u programs requested, exceeds MPTS_MAX_PROGRAMS (%u)                                    |
| 000310  | LE                | http request too long                                                                          |
| 000311  | LE                | unknown interface: %s                                                                          |
| 000312  | LE                | bad group address: %s                                                                          |
| 000313  | LE                | bad source address: %s                                                                         |
| 000314  | LA                | multi-input: no -e given, defaulting to %lds per-input retry                                   |
| 000315  | LW                | read error, retrying in %lds                                                                   |
| 000316  | LW                | read error, not retrying                                                                       |
| 000317  | LE                | rist: input uri must be rist://@host:port (listen)                                             |
| 000318  | LE                | rist: invalid peer count %d                                                                    |
| 000319  | LE                | rtmpout: invalid url: %.*s[redacted]@%s                                                        |
| 000320  | LE                | rtmpout: invalid url: %s                                                                       |
| 000321  | LE                | srt: rendezvous requires a local bind address                                                  |
| 000322  | LE                | srt: invalid peer count %d                                                                     |
| 000323  | LE                | srt: plain connection needs exactly one peer, got %d                                           |
| 000324  | LE                | srt: rendezvous cannot be combined with bonded groups                                          |
| 000325  | LW                | cpu-affinity: %s: cpu %u not in allowed set                                                    |
| 000326  | LE                | mapping: cannot open %s                                                                        |
| 000327  | LE                | mapping: line %d: expected id,uri,tsid,onid,sid                                                |
| 000328  | DW                | biss-ca: cannot access %s                                                                      |
| 000329  | DW                | biss-ca: cannot open directory %s                                                              |
| 000330  | DS                | -f mkv/mka requires exactly one -o file target (plus optional rtmp(s) targets)                 |
| 000331  | DA                | --insecure has no effect without -u or -o rtmps://                                             |
| 000332  | DA                | --strip-lcevc has no effect, no -f mkv/mka or -o rtmp(s):// target                             |
| 000333  | DS                | --biss2-sw and --biss2-esw are mutually exclusive                                              |
| 000334  | DS                | --biss2-esw and --biss2-id must be given together                                              |
| 000335  | DS                | --biss1-sw is mutually exclusive with --biss2-sw/--biss2-esw                                   |
| 000336  | CS,DS,RS,TS       | --metrics-inspect-ts-pids requires --metrics-inspect-ts full                                   |
| 000337  | DA                | --rist-profile has no effect, -i isn't rist://                                                 |
| 000338  | DA                | --rist-encryption-type has no effect, -i isn't rist://                                         |
| 000339  | CS,DS,RS,TS       | --rist-encryption-type requires --rist-profile main                                            |
| 000340  | DS,TS             | --srt-pbkeylen-in requires --srt-passphrase-in                                                 |
| 000341  | DA                | --srt-*-in has no effect, -i isn't srt://                                                      |
| 000342  | DS,RS,TS          | --srt-pbkeylen requires --srt-passphrase                                                       |
| 000343  | DA                | --srt-* has no effect without -o srt://                                                        |
| 000344  | DS,RS             | invalid --biss2-sw: %s (32 hex chars)                                                          |
| 000345  | DS                | invalid --biss2-esw: %s (32 hex chars)                                                         |
| 000346  | DS                | invalid --biss2-id: %s (32 hex chars)                                                          |
| 000347  | DS,RS             | invalid --biss1-sw: %s (12 hex chars)                                                          |
| 000348  | DS                | too many --biss2-ca-key (max %d)                                                               |
| 000349  | DS                | invalid -i input: %s                                                                           |
| 000350  | DS                | invalid --token-header: %s                                                                     |
| 000351  | CS,DS             | too many -o targets (max %d)                                                                   |
| 000352  | CS,DS             | invalid -o target: %s                                                                          |
| 000353  | DS                | invalid -f format: %s (ts\|mkv\|mka)                                                           |
| 000354  | CS,DS             | invalid -p pmt-pid: %s (0x0010..0x1FFE, or "all")                                              |
| 000355  | DS                | invalid --ecm-profile: %s                                                                      |
| 000356  | DS                | invalid --max-services: %s (1..%u)                                                             |
| 000357  | CS,DS,RS          | invalid --rist-profile: %s (simple\|main)                                                      |
| 000358  | CS,DS,RS,TS       | invalid --rist-encryption-type: %s (128\|256)                                                  |
| 000359  | DS,TS             | invalid --srt-pbkeylen-in: %s (16\|24\|32)                                                     |
| 000360  | CS,DS,TS          | invalid --srt-latency-in: %s (1..60000 ms)                                                     |
| 000361  | DS,RS,TS          | invalid --srt-pbkeylen: %s (16\|24\|32)                                                        |
| 000362  | CS,DS,RS,TS       | invalid --srt-latency: %s (1..60000 ms)                                                        |
| 000363  | DA,RA,TA          | input not set (required unless given on the command line)                                      |
| 000364  | DA                | output not set (required unless given on the command line)                                     |
| 000365  | DA                | format mkv/mka requires exactly one file output target (plus optional rtmp(s) targets)         |
| 000366  | DA                | insecure needs unicast-emm or an rtmps:// output target                                        |
| 000367  | DA                | strip-lcevc has no effect without format mkv/mka or an rtmp(s):// output target                |
| 000368  | DA                | biss2.sw and biss2.esw are mutually exclusive                                                  |
| 000369  | DA                | biss2.esw requires biss2.id                                                                    |
| 000370  | DA                | biss2.id requires biss2.esw                                                                    |
| 000371  | DA                | biss1.sw is mutually exclusive with biss2.sw/biss2.esw                                         |
| 000372  | DA                | rist.profile needs input rist://                                                               |
| 000373  | DA                | rist.encryption-type needs input rist://                                                       |
| 000374  | CA,DA             | rist.encryption-type requires rist.profile main                                                |
| 000375  | CA,DA             | srt.passphrase-in must be 10..79 characters                                                    |
| 000376  | CA,DA             | srt.pbkeylen-in requires srt.passphrase-in                                                     |
| 000377  | DA                | srt.*-in settings need input srt://                                                            |
| 000378  | CA,DA,RA          | srt.passphrase must be 10..79 characters                                                       |
| 000379  | CA,DA,RA          | srt.pbkeylen requires srt.passphrase                                                           |
| 000380  | DA                | srt.* settings need an srt:// output target                                                    |
| 000381  | DS                | --ecm-profile: invalid value for %s: %s                                                        |
| 000382  | DS                | --ecm-profile: spec too long (max %zu bytes)                                                   |
| 000383  | DS                | --ecm-profile: malformed field %s (expected key=value)                                         |
| 000384  | DS                | --ecm-profile: unknown field %s                                                                |
| 000385  | DS                | more than one format.headers entry has no default position, set field_order explicitly         |
| 000386  | DS                | cw_group: cw appears more than once                                                            |
| 000387  | DS                | cw_group: cp_number appears more than once                                                     |
| 000388  | DS                | cw_group may only contain cp_number and/or cw                                                  |
| 000389  | DS                | cw_group must contain cw                                                                       |
| 000390  | DS                | *-ECB has no IV, iv must be none                                                               |
| 000391  | DS                | iv=none only legal with an ECB cipher                                                          |
| 000392  | DS                | *-GCM is a stream cipher, padding must be none                                                 |
| 000393  | DS                | truncate_tag must be 4 or 8                                                                    |
| 000394  | DS                | truncate_tag only applies to integrity=hmac-sha256                                             |
| 000395  | DS                | short_key_info is inert with short_key_source=truncate                                         |
| 000396  | DS                | short_key_source=separate_info needs hkdf=1                                                    |
| 000397  | DS                | header id "%s" is a reserved token keyword                                                     |
| 000398  | DS                | duplicate header id "%s"                                                                       |
| 000399  | DS                | integrity_order=before-encrypt needs an explicit field_order placing integrity_tag last        |
| 000400  | DS                | cw_count>1 needs an explicit field_order using the cw_group token                              |
| 000401  | DS                | cw_count>1 needs format.cw_group set                                                           |
| 000402  | DS                | cw_group is only legal when cw_count>1                                                         |
| 000403  | DS                | field_order must contain ecm_id exactly once iff include_ecm_id=1                              |
| 000404  | DS                | cw is only legal inside cw_group when cw_count>1                                               |
| 000405  | DS                | field_order must contain cw_group exactly once when cw_count>1                                 |
| 000406  | DS                | field_order: cp_number placement inconsistent with include_cp_number                           |
| 000407  | DS                | field_order must contain cw exactly once                                                       |
| 000408  | DS                | field_order must contain cp_number exactly once iff include_cp_number=1                        |
| 000409  | DS                | integrity_order=before-encrypt needs integrity_tag exactly once, last in field_order           |
| 000410  | DS                | integrity_tag in field_order needs integrity_order=before-encrypt                              |
| 000411  | DS                | field_order may not contain iv/ciphertext/gcm_tag (wire_order tokens)                          |
| 000412  | DS                | field_order references unknown header id "%s"                                                  |
| 000413  | DS                | wire_order: iv presence inconsistent with cipher/iv_source                                     |
| 000414  | DS                | wire_order must contain ciphertext exactly once                                                |
| 000415  | DS                | wire_order: gcm_tag presence inconsistent with cipher                                          |
| 000416  | DS                | wire_order: integrity_tag presence inconsistent with integrity settings                        |
| 000417  | DS                | wire_order may not contain ecm_id/cp_number/cw/cw_group (field_order tokens)                   |
| 000418  | DS                | wire_order references unknown header id "%s"                                                   |
| 000419  | DW                | emm cache file too large, ignoring: %s                                                         |
| 000420  | DS                | cannot open -i %s                                                                              |
| 000421  | DW                | invalid -u/--unicast-emm uri, ignoring: %s                                                     |
| 000422  | DE                | failed to read emm cache %s                                                                    |
| 000423  | CE,DE             | no PAT received, giving up                                                                     |
| 000424  | CA,DA             | -p ignored, single-program source                                                              |
| 000425  | DS                | MPTS source, pick one with -p <pid>, or -p all                                                 |
| 000426  | DS                | -f mkv/-o rtmp:// can't hold multiple programs, pick one with -p <pid>                         |
| 000427  | CS,DS             | -p 0x%04x not found in this MPTS                                                               |
| 000428  | FS                | missing -g range                                                                               |
| 000429  | FS                | missing -l listen                                                                              |
| 000430  | FS                | missing -I iface                                                                               |
| 000431  | FS                | --no-ret and --no-fcc together leave nothing to run                                            |
| 000432  | FS                | --rsi-mc-ret requires RET and MC RET (--no-ret/--no-mc-ret not given)                          |
| 000433  | FS                | invalid -B buffer: %s (ms)                                                                     |
| 000434  | FS                | invalid -F ff-port: %s                                                                         |
| 000435  | FS                | invalid --max-ret-clients: %s                                                                  |
| 000436  | FS                | invalid --ret-client-idle-timeout: %s (s)                                                      |
| 000437  | FS                | invalid --ret-client-rate: %s (packets/s, 0 = unlimited)                                       |
| 000438  | FS                | invalid --ret-mc-dedup: %s (ms, 0 = off)                                                       |
| 000439  | FS                | invalid --rsi-interval: %s (s)                                                                 |
| 000440  | FS                | --rsi-hostname too long: %s                                                                    |
| 000441  | FS                | invalid -G gop-cap: %s (ms)                                                                    |
| 000442  | FS                | invalid -C max-bursts: %s                                                                      |
| 000443  | FS                | invalid -X burst-multiplier: %s (1.0 < x <= 1e6)                                               |
| 000444  | FS                | invalid -D burst-duration-cap: %s (ms)                                                         |
| 000445  | FS                | invalid --max-buffer-fill-bound: %s (ms, 0 = no bound)                                         |
| 000446  | FS                | invalid --congestion-nack-threshold: %s (0 = disabled)                                         |
| 000447  | FS                | invalid --fcc-range: %s                                                                        |
| 000448  | FS                | invalid --fcc-client-range: %s                                                                 |
| 000449  | FS                | invalid --fcc-client-rate: %s (requests/s, 0 = unlimited)                                      |
| 000450  | FS                | invalid -g range: %s                                                                           |
| 000451  | FS                | invalid -M max-channels: %s                                                                    |
| 000452  | FS                | invalid --channel-idle-timeout: %s (s)                                                         |
| 000453  | FS                | invalid -R rtx-pt: %s (0..127)                                                                 |
| 000454  | FS                | invalid -w workers: %s                                                                         |
| 000455  | FS,YS             | invalid --cpu-affinity: %s (off, auto, or list like 2-5,8)                                     |
| 000456  | FA                | range not set (required unless given on the command line)                                      |
| 000457  | FA                | listen not set (required unless given on the command line)                                     |
| 000458  | FA                | iface not set (required unless given on the command line)                                      |
| 000459  | FA                | no-ret and no-fcc together leave nothing to run                                                |
| 000460  | FA                | rsi.mc-ret requires RET and MC RET (no-ret/no-mc-ret not set)                                  |
| 000461  | FS                | bad listen address: %s                                                                         |
| 000462  | FA                | --fcc-client-range not set, any source can request FCC bursts                                  |
| 000463  | FS                | %s: failed to parse -l address for RSI announcement                                            |
| 000464  | RS                | duplicate --sid %u                                                                             |
| 000465  | RS,TS             | need -m output multicast or at least one -R peer                                               |
| 000466  | RS                | --al-fec requires -r/--rtp output                                                              |
| 000467  | RA,TA             | --al-fec has no effect without -m                                                              |
| 000468  | RA,TA             | --rist-profile/--rist-secret/--rist-encryption-type/--rist-cname/--rist-buffer depend on -R    |
| 000469  | CS,RS,TS          | --rist-secret requires --rist-profile main                                                     |
| 000470  | RS                | invalid --srt-group-mode: %s (broadcast\|backup)                                               |
| 000471  | RS,TS             | bonding several -R srt:// peers requires --srt-group-mode                                      |
| 000472  | RS,TS             | --srt-group-mode has no effect with a single -R srt:// peer                                    |
| 000473  | RA,TA             | --srt-* has no effect without an -R srt:// peer                                                |
| 000474  | RS,TS             | --srt-passphrase must be 10..79 characters                                                     |
| 000475  | RS                | --cas-* options require --cas-algo                                                             |
| 000476  | RS                | invalid --cas-algo: %s (cissa\|csa2\|csa1)                                                     |
| 000477  | RS                | too many --cas-ecmg vendors (max %d)                                                           |
| 000478  | RS                | invalid --cas-ecmg endpoint: %s                                                                |
| 000479  | RS                | invalid --cas-ecmg-version: %s (2\|3)                                                          |
| 000480  | RS                | invalid --cas-super-id: %s                                                                     |
| 000481  | RS                | invalid --cas-ecm-id: %s (1..65535)                                                            |
| 000482  | RS                | invalid --cas-ecm-pid: %s (0x0001..0x1FFE)                                                     |
| 000483  | RS                | --cas-emmg-listen: %s                                                                          |
| 000484  | RS                | invalid --cas-emmg-reverse endpoint: %s                                                        |
| 000485  | RS                | invalid --cas-emmg-version: %s (2\|3)                                                          |
| 000486  | RS                | invalid --cas-emmg-max-conns: %s (1..%u)                                                       |
| 000487  | RS                | invalid --cas-emm-pid: %s (0x0001..0x1FFE)                                                     |
| 000488  | RS                | invalid --cas-cp-duration: %s (ms, 1..86400000)                                                |
| 000489  | RS                | invalid --cas-resilience: %s (frozen\|cycling\|silent)                                         |
| 000490  | RS                | invalid --cas-cwenc-algo: %s (des56\|aes128\|aes256)                                           |
| 000491  | RS                | invalid --cas-cwenc-aes-mode: %s (stream\|ecb)                                                 |
| 000492  | RS                | invalid --biss2-emit-esw: %s (32 hex chars)                                                    |
| 000493  | RS                | invalid --biss2-ca-session-id: %s (16 bit, dec or 0x-hex)                                      |
| 000494  | RS                | too many -i inputs (max %d)                                                                    |
| 000495  | RS,TS             | invalid -T ttl: %s (1..255)                                                                    |
| 000496  | RS                | --sdt/-s must follow -i                                                                        |
| 000497  | RS                | --provider must follow -i                                                                      |
| 000498  | RS,TS             | invalid -e seconds: %s                                                                         |
| 000499  | RS,TS             | invalid --tsid: %s (1..65535)                                                                  |
| 000500  | RS,TS             | invalid --onid: %s (1..65535)                                                                  |
| 000501  | RS                | --sid must follow -i                                                                           |
| 000502  | RS,TS             | invalid --sid: %s (1..65535)                                                                   |
| 000503  | RS                | --jitter-ms must follow -i                                                                     |
| 000504  | RS,TS             | invalid --jitter-ms: %s (1..%d ms)                                                             |
| 000505  | RS                | -R: rist:// and srt:// peers cannot mix in one run                                             |
| 000506  | RS                | too many -R peers (max %d)                                                                     |
| 000507  | RS                | too many -R srt:// peers (max %d)                                                              |
| 000508  | RS                | -R srt:// output always calls out, no listener mode                                            |
| 000509  | RS                | invalid -R srt uri: %s                                                                         |
| 000510  | RS                | invalid -R uri: %s (must start with rist:// or srt://)                                         |
| 000511  | CS,RS,TS          | invalid --rist-buffer: %s (1-60000 ms)                                                         |
| 000512  | RS                | --%s must follow --cas-ecmg                                                                    |
| 000513  | RA,TA             | mcast, rist or srt not set (one is required unless given on the command line)                  |
| 000514  | RA                | al-fec requires rtp                                                                            |
| 000515  | RA                | al-fec has no effect without mcast                                                             |
| 000516  | RA                | profile/secret/encryption-type/cname/buffer have no effect without rist                        |
| 000517  | RA                | rist-secret requires rist-profile main                                                         |
| 000518  | RA                | rist-encryption-type requires rist-profile main                                                |
| 000519  | RA                | bonding several srt:// peers requires srt.group-mode                                           |
| 000520  | RA                | srt.group-mode has no effect with a single srt:// peer                                         |
| 000521  | RA                | srt.* settings need an srt:// peer                                                             |
| 000522  | RA                | cas.* options require cas.algo                                                                 |
| 000523  | RW                | input %u (%s): no data for %.0fs                                                               |
| 000524  | RE                | input %u (%s): invalid uri: %s                                                                 |
| 000525  | RE                | input %u (%s): empty response from %s                                                          |
| 000526  | RW                | input %u (%s): too many playlist redirects                                                     |
| 000527  | CS                | at most one -o rist:// target                                                                  |
| 000528  | CS                | -i rist:// and -o rist:// cannot combine                                                       |
| 000529  | CS                | --ret requires -i rtp://, no RTP sequence numbers otherwise                                    |
| 000530  | CS                | --ret family must match -i's for the SSM repair join; use --no-ret-mc otherwise                |
| 000531  | CS                | --pace requires -i - or -i <path>                                                              |
| 000532  | CS                | -f mkv/mka/mp4/m4a requires exactly one -o file target (plus optional rtmp(s) targets)         |
| 000533  | CS                | -f raw is incompatible with an -o rtmp://rtmps:// target                                       |
| 000534  | CS                | --out-iface needs -o rtp:// or udp:// target                                                   |
| 000535  | CS                | --ttl needs -o rtp:// or udp:// target                                                         |
| 000535  | VA                | --ttl/--dscp need -o rtp:// or udp:// target                                                   |
| 000536  | CS                | --al-fec-port needs --al-fec                                                                   |
| 000537  | CS                | --al-fec needs -i rtp:// source                                                                |
| 000538  | CS                | --insecure needs -o rtmps:// target or -i https:// source                                      |
| 000539  | CA                | --strip has no effect outside -f ts                                                            |
| 000540  | CS                | -s srt requires -f mkv, mka, mp4 or m4a                                                        |
| 000541  | CS                | --rist-profile/--rist-secret/--rist-encryption-type/--rist-cname/--rist-buffer need -o rist:// |
| 000542  | CS,TS             | --rist-encryption-type-in requires --rist-profile-in main                                      |
| 000543  | CS                | --rist-profile-in/--rist-encryption-type-in need -i rist:// source                             |
| 000544  | CS                | --srt-*-in needs -i srt:// source                                                              |
| 000545  | CS                | --srt-* needs -o srt:// target                                                                 |
| 000546  | CS                | invalid -i uri: %s                                                                             |
| 000547  | CS                | invalid -a track: %s (1..N or "all")                                                           |
| 000548  | CS                | invalid -f format: %s (raw\|ts\|mkv\|mka\|mp4\|m4a)                                            |
| 000549  | CS                | invalid -s: %s (strip\|keep\|srt)                                                              |
| 000550  | CS                | invalid -t duration: %s                                                                        |
| 000551  | CS                | invalid --sub-lead: %s (0..10000 ms)                                                           |
| 000552  | CS,TS             | invalid --rist-encryption-type-in: %s (128\|256)                                               |
| 000553  | CS,TS             | invalid --rist-profile-in: %s (simple\|main)                                                   |
| 000554  | CS                | invalid --ret addr:port: %s                                                                    |
| 000555  | CS                | invalid --ret-pt: %s (0..127)                                                                  |
| 000556  | CS                | invalid --ret-wait: %s (ms)                                                                    |
| 000557  | CS                | invalid --strip: %s (comma list NUL,NIT,AIT,EIT,CAT,ECM,EMM,RST,TDT,TOT,INT,LCEVC, or "none")  |
| 000558  | CS,VS             | invalid --ttl: %s (0..255)                                                                     |
| 000559  | CS                | invalid %s: %s (16\|24\|32)                                                                    |
| 000560  | CS                | --srt-passphrase%s must be 10..79 characters                                                   |
| 000561  | CS                | --srt-pbkeylen%s requires --srt-passphrase%s                                                   |
| 000562  | CA                | at most one rist:// out target                                                                 |
| 000563  | CA                | in rist:// and out rist:// cannot combine                                                      |
| 000564  | CA                | ret.addr requires in rtp://                                                                    |
| 000565  | CA                | ret.addr family must match in's, or set ret.no-mc                                              |
| 000566  | CA,VA             | pace requires in - or a file path                                                              |
| 000567  | CA                | format mkv/mka/mp4/m4a requires exactly one file out target (plus optional rtmp(s) targets)    |
| 000568  | CA                | format raw is incompatible with rtmp(s) out targets                                            |
| 000569  | CA                | subtitles srt requires format mkv, mka, mp4 or m4a                                             |
| 000570  | CA                | rist.secret requires rist.profile main                                                         |
| 000571  | CA                | rist.encryption-type-in requires rist.profile-in main                                          |
| 000572  | CA                | srt.* settings need an srt:// out target                                                       |
| 000573  | CW                | audio track %u not found (%d present)                                                          |
| 000574  | CA                | -p has no effect with -f raw                                                                   |
| 000575  | CS                | MPTS source - pick one with -p <pid>, or -p all                                                |
| 000576  | CS                | -f mkv/mp4/-o rtmp:// can't hold multiple programs, pick one with -p <pid>                     |
| 000577  | TA                | biss: --cas-pids resolved to no pids, nothing to scramble                                      |
| 000578  | TA                | biss-ca: --cas-pids resolved to no pids, nothing to scramble                                   |
| 000579  | TA                | cas: source declares no PCR_PID, cannot drive crypto-period timing                             |
| 000580  | TA                | cas: --cas-pids resolved to no pids, nothing to scramble                                       |
| 000581  | TA                | biss: --cas-pids resolved to no pids across any program, nothing to scramble                   |
| 000582  | TA                | biss-ca: --cas-pids resolved to no pids across any program, nothing to scramble                |
| 000583  | TA                | cas: --cas-pids resolved to no pids across any program, nothing to scramble                    |
| 000585  | TS                | at most one -i rist:// input                                                                   |
| 000586  | TS                | -i rist:// and -R rist:// cannot combine                                                       |
| 000587  | TS                | -S/--stuff and -B/--burst-limit need -b/--bitrate                                              |
| 000588  | TS                | --pcr-mode regenerate needs -b/--bitrate and -S/--stuff                                        |
| 000589  | TA                | --pcr-mode regenerate without -B/--burst-limit: output can run ahead of the PCR timeline       |
| 000590  | TS                | --pcr-lead-ms needs --pcr-mode regenerate                                                      |
| 000591  | LS,TS             | --metrics/--metrics-interval require --metrics-id                                              |
| 000592  | LS,TS             | --metrics-inspect-ts requires --metrics-id                                                     |
| 000593  | TS                | --al-fec requires RTP output, not -u/--udp                                                     |
| 000594  | TS                | --srt-passphrase-in must be 10..79 characters                                                  |
| 000595  | TA                | --srt-*-in has no effect, that -i isn't srt://                                                 |
| 000596  | TA                | --rist-*-in has no effect, that -i isn't rist://                                               |
| 000597  | TA                | --jitter-ms has no effect, that -i isn't rtp/udp/rist/srt                                      |
| 000598  | TS                | --hbbtv requires --hbbtv-org-id and --hbbtv-app-id                                             |
| 000599  | TS                | --hbbtv-org-id/--hbbtv-app-id/--hbbtv-name needs --hbbtv                                       |
| 000600  | TS                | --cas-* options require --cas-algo                                                             |
| 000601  | TS                | duplicate --sid                                                                                |
| 000602  | TS                | invalid --cas-pids: %s                                                                         |
| 000603  | TS                | invalid -b bitrate: %s (kbps)                                                                  |
| 000604  | TS                | invalid -p pmt-pid: %s (0x0010..0x1FFE)                                                        |
| 000605  | TS                | invalid --strip: %s (comma list of DATA,ECM, or "none")                                        |
| 000606  | TS                | invalid --hbbtv-org-id: %s                                                                     |
| 000607  | TS                | invalid --hbbtv-app-id: %s                                                                     |
| 000608  | TS                | must follow -i                                                                                 |
| 000609  | TS                | must follow --cas-ecmg                                                                         |
| 000610  | TS                | <option>: %s                                                                                   |
| 000611  | TE                | -p 0x%x not present in PAT                                                                     |
| 000612  | TW                | cas: --cas-pids video/audio/lcevc: not every -i discovered within %.0fs, starting without:     |
| 000613  | TW                | input %u: %s                                                                                   |
| 000614  | TW                | input %u: no live PMT found within %.0fs                                                       |
| 000615  | TW                | input error, retrying in %lds                                                                  |
| 000616  | TW                | no live PMT found within %.0fs (use -p to select one, or check the source)                     |
| 000617  | TW                | retrying in %lds                                                                               |
| 000618  | YS                | invalid %s (need user:password)                                                                |
| 000619  | YS                | %s credentials too long                                                                        |
| 000620  | YS                | --tls-cert given without --tls-key                                                             |
| 000621  | YS                | --tls-key given without --tls-cert                                                             |
| 000622  | YS                | --hls-part-size (%.2f) must be smaller than --segment-size (%.2f)                              |
| 000623  | YS                | --dash-part-size (%.2f) must be smaller than --segment-size (%.2f)                             |
| 000624  | YS                | --ssdp-max-age (%u) must be at least 2x --ssdp-interval (%.2f)                                 |
| 000625  | YS                | --enable-dlna requires spts in -f/--format (DLNA playback always uses /spts)                   |
| 000626  | YS                | --enable-dlna requires rawaudio in -f/--format (DLNA radio items use /rawaudio)                |
| 000627  | YS                | --enable-dlna needs --dlna-host (or a concrete -l/--listen address, not 'all')                 |
| 000628  | YS                | invalid --sds-timeout: %s (seconds, > 0)                                                       |
| 000629  | YS                | invalid --sds-refresh-interval: %s (seconds, > 0)                                              |
| 000630  | YS                | invalid --ssdp-ttl: %s (1..255)                                                                |
| 000631  | YS                | invalid --ssdp-interval: %s (seconds, > 0)                                                     |
| 000632  | YS                | invalid --ssdp-max-age: %s (seconds, > 0)                                                      |
| 000633  | YS                | -i %s: %s                                                                                      |
| 000634  | YS                | -n/--name %s: %s                                                                               |
| 000635  | YS                | --media-type %s: %s                                                                            |
| 000636  | YS                | invalid --h3-altsvc-port: %s (1..65535)                                                        |
| 000637  | YS                | invalid --h3-max-streams: %s (4..1000)                                                         |
| 000638  | YS                | invalid --h3-max-conns: %s (1..65536)                                                          |
| 000639  | YS                | invalid --h3-idle-timeout: %s (seconds, 1..86400)                                              |
| 000640  | YS                | invalid --h3-retry: %s                                                                         |
| 000641  | YS                | invalid --h3-max-udp-payload: %s (1200..65507)                                                 |
| 000642  | YS                | invalid --h3-window: %s (KiB, 16..1048576)                                                     |
| 000643  | YS                | invalid --h3-cc: %s                                                                            |
| 000644  | YS                | invalid -l/--listen address: %s                                                                |
| 000645  | YS                | invalid -L/--listen-tls address: %s                                                            |
| 000646  | YS                | invalid -j/--workers: %s (-1/-2/-3, or a positive count)                                       |
| 000647  | YS                | invalid --max-clients: %s (1..65536)                                                           |
| 000648  | YS                | invalid --max-channels: %s (1..1024)                                                           |
| 000649  | YS                | invalid --idle-timeout: %s (seconds, 0..86400, 0 = off)                                        |
| 000650  | YS                | invalid --capture-ring-size: %s (KiB, 1..262144)                                               |
| 000651  | YS                | invalid --ts-startup-timeout: %s (seconds, 0=off)                                              |
| 000652  | YS                | invalid --segment-size: %s (seconds, min 2)                                                    |
| 000653  | YS                | invalid --segment-count: %s (3-%d)                                                             |
| 000654  | YS                | invalid --hls-part-size: %s (seconds, 0.05-5.0)                                                |
| 000655  | YS                | invalid --dash-part-size: %s (seconds, 0.05-5.0)                                               |
| 000656  | YS                | invalid --dash-utc-url: too long (max 256 chars)                                               |
| 000657  | YS                | invalid --hls-seg-pool: %s (min 1)                                                             |
| 000658  | YS                | invalid -f/--format: %s (comma-separated list of ts,spts,rawaudio,hls,llhls,dash,lldash)       |
| 000659  | YS                | invalid --max-variants: %s (1..1024)                                                           |
| 000660  | YA                | tls.key given without tls.cert                                                                 |
| 000661  | YA                | hls.part-size must be smaller than segment-size                                                |
| 000662  | YA                | dash.part-size must be smaller than segment-size                                               |
| 000663  | YA                | ssdp.max-age must be at least 2x ssdp.interval                                                 |
| 000664  | YA                | enable-dlna requires spts in format                                                            |
| 000665  | YA                | enable-dlna requires rawaudio in format                                                        |
| 000666  | YA                | enable-dlna needs dlna.host or a concrete listen address, not 'all'                            |
| 000667  | YW                | --status-tpl %s: cannot read, using built-in page                                              |
| 000668  | YW                | --status-tpl %s: reload failed, keeping previous content                                       |
| 000669  | YS                | -j resolves to %d workers, exceeds max supported %d                                            |
| 000670  | YA                | --cpu-affinity lists %u cpus for %d workers, rest float                                        |
| 000671  | YS                | tls_init: fd table cap %d exceeds max supported %d                                             |
| 000672  | YE                | capture: source read error                                                                     |
| 000673  | YW                | capture: reader fell behind, dropped %llu bytes                                                |
| 000674  | YE                | http source %s: invalid URL, source left unavailable                                           |
| 000675  | YE                | --rist %s: %s                                                                                  |
| 000676  | YE                | -i -: %s                                                                                       |
| 000677  | YW                | --m3u %s: cannot open/parse, list left empty                                                   |
| 000678  | YW                | --xspf %s: cannot open/parse, list left empty                                                  |
| 000679  | YW                | --csv %s: cannot open/parse, list left empty                                                   |
| 000680  | YW                | --xml %s: cannot open, list left empty                                                         |
| 000681  | YW                | --xml %s: cannot read, list left empty                                                         |
| 000682  | YW                | --xml %s: no services found, list left empty                                                   |
| 000683  | YE                | --sds %s: invalid address, list left empty                                                     |
| 000685  | LS                | %s too long                                                                                    |
| 000686  | LS                | %s must be 10..79 characters                                                                   |
| 000687  | LS                | invalid --metrics-interval: %s (seconds, 1..86400)                                             |
| 000688  | LS                | invalid --metrics-inspect-ts-pids: %s (comma separated pids, 0..8191)                          |
| 000689  | LS                | invalid --metrics-inspect-ts: %s (off\|basic\|medium\|full)                                    |
| 000690  | GS                | --timeout is listen-only (use -t/--interval)                                                   |
| 000691  | GS                | --interval is announce-only (use -t/--timeout)                                                 |
| 000692  | GS                | --refresh is announce-only                                                                     |
| 000693  | GS                | invalid --refresh cycles: %s                                                                   |
| 000694  | FS                | --fcc-resolve-by-port port range %lu..%lu exceeds 65535 (lower --fcc-resolve-base-port or -M)  |
| 000695  | AE                | %s: %zu bytes, max %u                                                                          |
| 000696  | FS                | --fcc-resolve-by-port port range %lu..%lu contains the -l port %u                              |
| 000698  | AS                | --fcc-resolve-by-port port range %lu..%lu exceeds 65535                                        |
| 000699  | AS                | --al-fec-* options are announce-only                                                           |
| 000700  | YA                | tls.cert given without tls.key                                                                 |
| 000701  | SS                | -m range too large: %s (max %u addresses)                                                      |
| 000702  | CS                | invalid --al-fec-out: %s (want L:D, L*D<=400, L<=40)                                           |
| 000703  | CS                | invalid --al-fec-port-out: %s                                                                  |
| 000704  | CS                | --al-fec-out requires --al-fec-port-out                                                        |
| 000705  | CS                | --al-fec-port-out needs --al-fec-out                                                           |
| 000706  | CS                | --al-fec-out needs -o rtp:// target                                                            |
| 000707  | CA                | al-fec-out requires al-fec-port-out                                                            |
| 000713  | MS                | %s exists and is not a socket                                                                  |
| 000714  | MS                | %s is in use by another process                                                                |
| 000715  | MS                | empty -S path                                                                                  |
| 000716  | MS                | empty --tls-cert path                                                                          |
| 000717  | MS                | empty --tls-key path                                                                           |
| 000718  | RS,TS             | --cas-%s-pid 0x%04X collides with a PID the muxer emits                                        |
| 000719  | VS                | --encryption-type requires --secret                                                            |
| 000720  | VA                | encryption-type requires secret                                                                |
| 000721  | HA                | pace requires in - , a file path or http(s)://                                                 |
| 000722  | HA,HS             | --local and srt:// peer differ in address family                                               |
| 000723  | 3A,3S             | invalid -s serial: longer than %d bytes                                                        |
| 000724  | TS                | invalid --rist-buffer-in: %s (1-60000 ms)                                                      |
| 000725  | TS                | --rist-secret-in requires --rist-profile-in main                                               |
| 000726  | TS                | CISSA/BISS-2 needs a build with OpenSSL                                                        |
| 000727  | TS                | invalid --al-fec-in: %s (want L:D, L*D<=400, L<=40)                                            |
| 000728  | TS                | invalid --al-fec-port-in: %s                                                                   |
| 000729  | TS                | --al-fec-in requires --al-fec-port-in                                                          |
| 000730  | TA                | --al-fec-in has no effect, that -i isn't rtp://                                                |
| 000731  | TA                | --al-fec-port-in has no effect without --al-fec-in                                             |

### 1xxxxx Output

| Number  | Codes          | Message                                                         |
|---------|----------------|-----------------------------------------------------------------|
| 100100  | GD             | cycle %u sent, %d fragments in %d containers                    |
| 100101  | AS,GE,GS       | cannot open %s for writing                                      |
| 100102  | AE,BE,GE,SE,XE | error writing %s                                                |
| 100103  | BE             | write failed                                                    |
| 100104  | BS,XS          | cannot open %s                                                  |
| 100106  | 3E             | send %s failed: %s                                              |
| 100107  | VW             | rist output: write failed, will keep retrying                   |
| 100108  | CS,LS,SS       | open %s: %s                                                     |
| 100109  | AE             | SD&S document too large (max %d bytes), reduce the service list |
| 100110  | AE             | Package Discovery document too large (max %d bytes)             |
| 100111  | AE             | Regionalisation Discovery document too large (max %d bytes)     |
| 100112  | AE             | RMS-FUS Discovery document too large (max %d bytes)             |
| 100113  | AD             | cycle %u sent                                                   |
| 100114  | HA             | --send-buffer-mult needs -o srt://                              |
| 100118  | LE             | ecmg: reply send failed                                         |
| 100119  | LE             | ecmg: CW_provision send failed                                  |
| 100120  | LE             | emmg: reply send failed (slot %d)                               |
| 100121  | LW             | input %u (%s): DASH output buffer full, dropped bytes           |
| 100122  | LW             | input %u (%s): HLS output buffer full, dropped bytes            |
| 100123  | LE             | mkv write: %s                                                   |
| 100124  | LE             | mkv patch: %s                                                   |
| 100125  | LE             | mp4 write: %s                                                   |
| 100126  | LE             | send: %s                                                        |
| 100127  | LE             | sendto: %s                                                      |
| 100128  | LE             | sendmsg: %s                                                     |
| 100129  | LE             | rist: write failed                                              |
| 100130  | LW             | %s output: send failed, will keep retrying                      |
| 100131  | LW             | srt output: send queue full, dropping oldest queued chunk       |
| 100132  | DE,LE          | output write failed: %s                                         |
| 100133  | DW             | cannot write emm cache %s: %s                                   |
| 100134  | DE             | short write to emm cache %s                                     |
| 100135  | DE             | error writing emm cache %s: %s                                  |
| 100136  | DS             | cannot open -o %s: %s                                           |
| 100137  | DW             | rtmp[%d] output: write failed, will keep retrying               |
| 100138  | FE             | unicast sendto: %s                                              |
| 100139  | CE             | output write failed: %s                                         |
| 100140  | CW             | %s output: write failed, will keep retrying                     |
| 100141  | GE             | container build failed                                          |
| 100142  | GE             | wrapper build failed                                            |
| 100143  | GE             | guide too large (%zu bytes, max %u), reduce window or channels  |
| 100144  | AE,GE          | send failed                                                     |
| 100145  | GE             | channel %s exceeds %u B per repository, reduce its programmes   |
| 100146  | GW             | channel %s: line break in id or uri, skipped in csv map         |
| 100147  | GE             | guide needs more than %u segments, reduce channels              |
| 100148  | TW             | output continuity gap on pid 0x%04x, cc %u after %u             |
| 100149  | DE             | cannot replace emm cache %s: %s                                 |

### 3xxxxx Authentication and cryptography

| Number  | Codes | Message                                                                                   |
|---------|-------|-------------------------------------------------------------------------------------------|
| 300100  | 3E    | encrypt failed building %s                                                                |
| 300101  | 3D    | ECM request srvid=%04X caid=%04X prid=%08X len=%zu                                        |
| 300102  | 3D    | EMM caid=%04X provid=%08X len=%zu                                                         |
| 300103  | 3W    | username mismatch, closing (slot %d)                                                      |
| 300104  | 3W    | checksum error, wrong password? (slot %d)                                                 |
| 300105  | 3S    | %s: cannot load RSA private key from -k %s                                                |
| 300106  | MW,YW | SIGUSR1: TLS cert reload failed                                                           |
| 300107  | LW    | biss-ca: entitlement_key_id derivation failed for a key in %s, skipping                   |
| 300108  | LW    | biss-ca: cannot open --biss2-ca-receivers %s: %s                                          |
| 300109  | LW    | biss-ca: %s holds no readable PEM public key, skipping                                    |
| 300110  | LW    | biss-ca: %d key(s) in %s dropped, past the %d-section EMM limit                           |
| 300111  | LW    | biss-ca: %d duplicate key(s) in %s ignored                                                |
| 300112  | LW    | biss-ca: %d file(s) in %s not usable as receiver keys                                     |
| 300113  | LS    | biss-ca: initial key generation failed (RNG unavailable)                                  |
| 300114  | LS    | biss-ca: initial key rejected by the scrambler, not starting                              |
| 300115  | LE    | biss-ca: SW rotation failed (RNG unavailable), keeping current key                        |
| 300116  | LE    | biss-ca: SK rotation failed (RNG unavailable), keeping current key                        |
| 300117  | LE    | biss-ca: ECM rebuild failed (RNG unavailable)                                             |
| 300118  | LE    | biss-ca: ECM rebuild failed (AES-CBC)                                                     |
| 300119  | LE    | biss-ca: ECM section build overflow                                                       |
| 300120  | LE    | biss-ca: EMM rebuild failed (session_data build)                                          |
| 300121  | LW    | biss-ca: RSA-OAEP encrypt failed for one receiver, dropping it this round                 |
| 300122  | LE    | biss-ca: EMM section %zu build overflow (%zu receivers)                                   |
| 300123  | LW    | biss-ca: reload of %s produced zero usable receivers, keeping the previous list           |
| 300124  | LA    | biss-ca: this build has no OpenSSL, BISS Mode CA unavailable                              |
| 300125  | LA    | biss: this build has no OpenSSL, BISS Mode E (ESW) unavailable                            |
| 300126  | LS    | %sbiss: %s key rejected by the scrambler, not starting                                    |
| 300127  | LS    | %sbiss: failed to compute ESW (no OpenSSL in this build?)                                 |
| 300128  | LS    | %sbiss-ca: failed to generate a random entitlement_session_id (no OpenSSL in this build?) |
| 300129  | LE    | %sEMM-U decrypt failed                                                                    |
| 300130  | LE    | %sEMM-G decrypt failed for service %04X                                                   |
| 300131  | LA    | cw_encryption: this build has no OpenSSL, CW_encryption unavailable                       |
| 300132  | LE    | ecmg: CW source failed for CP %u                                                          |
| 300133  | LE    | input %u (%s): HLS key fetch returned %zu bytes, expected %d                              |
| 300134  | LW    | input %u (%s): HLS segment decrypt failed, skipped                                        |
| 300135  | LA    | rtmp: authmod=adobe needs OpenSSL, built without it, credentials ignored                  |
| 300136  | LE    | %s: unknown TLS error                                                                     |
| 300137  | LE    | tls handshake with %s failed                                                              |
| 300138  | LE,LW | tls handshake failed                                                                      |
| 300139  | LS    | tls: failed to load cert %s / key %s                                                      |
| 300140  | LA    | tls: this build has no TLS support, cannot serve https                                    |
| 300141  | LA    | tls: this build has no TLS support, cannot connect to %s over https                       |
| 300142  | LA    | cissa: this build has no OpenSSL, CISSA scrambling unavailable                            |
| 300143  | LA    | csa2: libdvbcsa not available at runtime (%s), CSA2 scrambling unavailable                |
| 300144  | LA    | csa2: this build has no libdvbcsa, CSA2 scrambling unavailable                            |
| 300145  | DW    | biss-ca: %s holds no readable PEM private key                                             |
| 300146  | DW    | biss-ca: %s holds no readable PEM private key, skipping                                   |
| 300147  | DE    | ecm_profile: decrypt or integrity check failed                                            |
| 300148  | DE    | ecm_profile: CW resolve failed for this ECM section                                       |
| 300149  | DW    | BISS Mode CA stream detected, no --biss2-ca-key given                                     |
| 300150  | DW    | no usable RSA private key in --biss2-ca-key                                               |
| 300151  | DE    | failed to set up BISS-CA descrambler                                                      |
| 300152  | DW    | BISS stream detected, no --biss1-sw/--biss2-sw/--biss2-esw given                          |
| 300153  | DW    | failed to decrypt --biss2-esw (no OpenSSL in this build?)                                 |
| 300154  | DE    | failed to set up BISS descrambler                                                         |
| 300155  | DE    | ECM/EMM CAS detected, no -k/-s/-e given, giving up                                        |
| 300156  | DE    | cannot load RSA private key from -k %s                                                    |
| 300157  | FS    | %s: failed to drop privileges to -u %s                                                    |
| 300158  | RA    | cas: no audio pids to scramble                                                            |
| 300159  | RS    | cas: %zu audio pids exceeds the %d cas can scramble in one session                        |
| 300160  | RW    | PMT: CA descriptor build failed, PMT not sent                                             |
| 300162  | RE    | cas: fatal, stopping                                                                      |
| 300163  | RS,TS | cas: failed to start                                                                      |
| 300164  | TS    | source CA/ECM passthrough disabled: --cas-algo/--biss* already scrambling this mux        |
| 300165  | TW    | program %u: cas pid set full, %zu new stream%s go out unscrambled                         |
| 300166  | TW    | program %u: CA descriptor build failed, PMT not sent                                      |
| 300167  | TE    | cas: fatal error, stopping                                                                |
| 300168  | TS    | cas setup failed                                                                          |
| 300169  | YE    | http3: RAND_bytes failed                                                                  |
| 300170  | YA    | TLS init failed, --listen-tls not bound                                                   |
| 300171  | YA    | no usable TLS certificate found, --listen-tls not bound                                   |
| 300172  | MA    | --auth is visible in the process list, use 'auth' in the config file                      |
| 300173  | LE    | http: refusing https to http redirect to %s                                               |
| 300174  | DE    | scrambler rejected CW (parity=%s), keeping previous key                                   |
| 300175  | DE    | BISS descrambler rejected the key (no libdvbcsa/OpenSSL in this build?)                   |
| 300176  | DE    | failed to set up CAS descrambler                                                          |
| 300177  | LE    | biss-ca: SW rotation failed (key rejected by the scrambler), keeping current key          |

### 4xxxxx Format

| Number  | Codes    | Message                                                                        |
|---------|----------|--------------------------------------------------------------------------------|
| 400100  | GE       | dipibcg: no zlib support, cannot decompress received container                 |
| 400101  | BE       | no channels or programmes found in input XML                                   |
| 400102  | BE       | BiM encoding failed                                                            |
| 400103  | BE       | input truncated: %zu bytes, need 4 byte length header                          |
| 400104  | BE       | input truncated: bitstream needs %zu bytes, %zu present                        |
| 400105  | BE       | string repository missing or unsupported encoding                              |
| 400106  | BE       | malformed or truncated BiM stream                                              |
| 400107  | BE       | nothing to encode, no channel with IPTV ServiceURL                             |
| 400107  | 3W       | connection id changed mid-stream, closing (slot %d)                            |
| 400108  | BE       | encoded size exceeds 4 GiB format limit: %zu+%zu bytes                         |
| 400108  | 3W       | oversized request (%zu), closing (slot %d)                                     |
| 400109  | SW       | %u/%u %-28s stream present, no program resolved                                |
| 400110  | AE       | line %d: bad uri: %s                                                           |
| 400111  | AE       | malformed #EXTINF line: %s                                                     |
| 400112  | AE       | bad uri: %s                                                                    |
| 400113  | AE       | %s: no recognized SD&S root element                                            |
| 400114  | AE       | %s: unrecognized suffix, expected .csv/.m3u/.m3u8/.xspf/.xml                   |
| 400115  | AE       | %s: line %d: expected id,name,lang,visible,svc1\|svc2\|...                     |
| 400116  | AE       | %s: line %d: bad package id: %s                                                |
| 400117  | AE       | %s: line %d: bad lang: %s                                                      |
| 400118  | AE       | %s: line %d: package has no services                                           |
| 400119  | AE       | %s: line %d: expected id,country,type:value,...                                |
| 400120  | AE       | %s: line %d: bad country code: %s                                              |
| 400121  | AE       | %s: line %d: bad CA entry: %s                                                  |
| 400122  | AE       | %s: line %d: bad CA type (0..255): %s                                          |
| 400123  | AE       | %s: line %d: cell has no CA entries                                            |
| 400124  | XS       | reverse map line %d: expected uri,id                                           |
| 400125  | LE       | ecmg: malformed or unusable channel_status                                     |
| 400126  | LE       | ecmg: failed to build CW_provision                                             |
| 400127  | LW       | emmg: dropping oversized EMM datagram                                          |
| 400128  | LW       | emmg: dropping datagram that is not a well-formed section                      |
| 400129  | LS       | nesting too deep                                                               |
| 400130  | LS       | mappings inside lists not supported                                            |
| 400131  | LS       | mapping used as key                                                            |
| 400132  | LS       | top level must be a mapping                                                    |
| 400133  | LS       | nested lists not supported                                                     |
| 400134  | LS       | list used as key                                                               |
| 400135  | LA,LS    | %s: options must be a mapping                                                  |
| 400136  | LS       | unbalanced mapping end                                                         |
| 400137  | LS       | key too long                                                                   |
| 400138  | LS       | yaml syntax error: %s (%s)                                                     |
| 400139  | LS       | yaml syntax error: %s                                                          |
| 400140  | LS       | multiple documents not supported                                               |
| 400141  | LS       | aliases not supported                                                          |
| 400142  | LW       | mpd: Representation id truncated to %zu bytes                                  |
| 400143  | LW       | %s: more than %d PMT candidates, dropping the rest                             |
| 400144  | LW       | %s: PAT section dropped: %s (table_id 0x%02x, len %zu)                         |
| 400145  | LW       | %s: PAT has more than %d programs, dropping the rest                           |
| 400146  | LW       | %s: PMT section dropped: %s (pid 0x%04x, program %u, table_id 0x%02x, len %zu) |
| 400147  | LW       | %s: PMT for program %u has more than %d ES entries, dropping the rest          |
| 400148  | LW       | %s: SDT section dropped: %s (table_id 0x%02x, len %zu)                         |
| 400149  | LW       | %s: NIT section dropped: %s (table_id 0x%02x, len %zu)                         |
| 400150  | LW       | %s: NIT network descriptor loop overruns section, dropped                      |
| 400151  | LW       | %s: CAT section dropped: %s (table_id 0x%02x, len %zu)                         |
| 400152  | LW       | %s: non-PMT sections on PMT pid 0x%04x (table_id 0x%02x), ignoring             |
| 400153  | LE       | ret: gap of %zu too large to track, resyncing at seq %u                        |
| 400154  | LE       | ret: bad --ret address: %s                                                     |
| 400155  | LE       | input %u (%s): HLS init segment unusable                                       |
| 400156  | LW       | HLS segment format not supported, skipped                                      |
| 400157  | LD       | rejected malformed snapshot (%zu bytes)                                        |
| 400158  | LD       | rejected snapshot with unsupported protocol version %u                         |
| 400159  | LD       | dropped orphan part %u for %s/%s                                               |
| 400160  | LD       | rejected malformed snapshot part for %s/%s                                     |
| 400161  | LD       | dropped oversized snapshot for %s/%s                                           |
| 400162  | LW       | flv=no_vc(%s)                                                                  |
| 400163  | LW       | flv: pes tracker full, pid 0x%04x not reassembled                              |
| 400164  | LW       | flv=no_ac(%s)                                                                  |
| 400165  | LW       | flv=no_mux                                                                     |
| 400166  | LW       | mkv: pes tracker full, pid 0x%04x not reassembled                              |
| 400167  | LW       | mkv=no_vc(%s)                                                                  |
| 400168  | LW       | mkv=no_ac(%s)                                                                  |
| 400169  | LW       | mkv=txtsub(%u=%s)                                                              |
| 400170  | LW       | mkv=no_mux                                                                     |
| 400171  | LW       | mp4: pes tracker full, pid 0x%04x not reassembled                              |
| 400172  | LW       | mp4=no_vc(%s)                                                                  |
| 400173  | LW       | mp4=no_ac(%s)                                                                  |
| 400174  | LW       | mp4=txtsub(%u=%s)                                                              |
| 400175  | LW       | mp4=no_mux                                                                     |
| 400176  | LW       | dvbstp: rejecting malformed packet (%s)                                        |
| 400177  | LE       | http: malformed response status line/headers                                   |
| 400178  | LW       | http: response has more than %d headers, dropping rest                         |
| 400179  | LE       | http: malformed content-length '%s'                                            |
| 400180  | LW       | http: unsupported transfer-encoding: %s                                        |
| 400181  | LE       | http: malformed chunked body                                                   |
| 400182  | LW       | bcg: channel %s has more than %d names, dropping "%s"                          |
| 400184  | LW       | bcg: attribute %s truncated to %zu bytes                                       |
| 400185  | LE       | mapping: line %d: bad tsid/onid/sid                                            |
| 400186  | LW       | bcg: <Name> text truncated to %zu bytes                                        |
| 400187  | LW       | bcg: <ServiceURL> text truncated to %zu bytes                                  |
| 400188  | LW       | bcg: <display-name> text truncated to %zu bytes                                |
| 400189  | LW       | xmltv: ignoring bad stop time: %s                                              |
| 400190  | LW       | xmltv: unknown time zone in stop time, no offset applied: %s                   |
| 400191  | LW       | xmltv: skipping programme without start or channel attribute                   |
| 400192  | LW       | xmltv: skipping programme, bad start time: %s                                  |
| 400193  | LW       | xmltv: unknown time zone in start time, no offset applied: %s                  |
| 400194  | DE       | unrecognized scrambling_mode 0x%02x                                            |
| 400195  | FW       | %s:%u payload %zu exceeds cap (%d), dropping until it fits again               |
| 400196  | FW       | NACK has more than %d FCI entries, dropping the rest                           |
| 400197  | RW       | input %u (%s): unrecognized audio sync                                         |
| 400198  | RW       | input %u (%s): HLS segment format not recognized, skipped                      |
| 400199  | TW       | cas: PCR discontinuity on pid 0x%x, resyncing                                  |
| 400200  | TE       | --hbbtv: AIT build failed (url too long?)                                      |
| 400201  | TW       | program %u: PMT section too small, some ES descriptors dropped                 |
| 400202  | YW       | hls: remlen exceeded HLS_AUDIO_REM_MAX, discarding pending bytes               |
| 400203  | YW       | hls: audio ES parse failed, resyncing byte by byte                             |
| 400204  | YE       | hls_set_init_segment: %zu exceeds HLS_INIT_SEG_MAX %d                          |
| 400205  | AE,LE,XS | mapping/reverse map line %d: longer than %zu bytes                             |
| 400206  | LE,XS    | mapping/reverse map line %d: id or uri longer than %zu bytes                   |
| 400207  | XS       | scan: line %d: longer than %d bytes                                            |
| 400208  | XS       | scan: entry %d: name or uri longer than %zu bytes                              |
| 400209  | XS       | scan: line %d: bad tsid/onid/sid                                               |
| 400210  | AE       | %s: line %d: bad visible flag, expected 0 or 1: %s                             |
| 400211  | AE       | %s: line %d: duplicate package id: %u                                          |
| 400214  | LW       | xmltv: skipping programme, bad start time: %s                                  |
| 400215  | AE       | bad %s (0..65535): %s                                                          |
| 400216  | AE       | line %d: bad service id field                                                  |
| 400217  | AE       | xspf track %d: bad service id field                                            |
| 400220  | AE       | %s: package %u: unknown service: %s                                            |
| 400223  | XW       | reverse map: duplicate uri %s, first entry wins                                |
| 400224  | XW       | duplicate channel id %s after reverse map                                      |
| 400225  | LW       | xmltv: <%s> at line %lu has no end tag, rest of file ignored                   |
| 400226  | RW       | audio frame of %zu bytes exceeds PES limit, dropped                            |
| 400227  | LW       | fMP4 remux: track %u parameter sets exceed %d bytes, track skipped             |
| 400228  | LW       | fMP4 remux: %u sample(s) dropped, conversion failed                            |
| 400229  | LW       | input %u (%s): %s segment exceeds %d bytes, skipped                            |
| 400230  | LE       | input %u (%s): %s %s exceeds %d bytes                                          |
| 400231  | BW       | channel %s: skipping programme, bad start/end time (start "%s", end "%s")      |
| 400232  | BW       | channel %s: no <ServiceURL name="IPTV">, skipping channel and its programmes   |
| 400233  | BW       | channel %s: unknown or skipped, skipping programme (start "%s")                |
| 400234  | LW       | bcg: channel %s: %d <Name> dropped (max %d names, %d bytes each)               |
| 400235  | LW       | input %u (%s): DASH segment unavailable, retrying                              |
| 400236  | LW       | input %u (%s): DASH fell behind live edge, rejoining                           |
| 400237  | LE       | input %u (%s): DASH init segment unavailable                                   |
| 400238  | LW       | input %u (%s): HLS segment unavailable, skipped                                |
| 400239  | LW       | fMP4 remux: track %u AAC without AudioSpecificConfig, track skipped            |

### 5xxxxx System

| Number  | Codes    | Message                                                                                          |
|---------|----------|--------------------------------------------------------------------------------------------------|
| 500100  | AS,GS    | cannot join %s                                                                                   |
| 500101  | *S       | out of memory                                                                                 |
| 500102  | 3E,3S,FS | pthread_create: %s                                                                               |
| 500103  | 3S,FS,LE | socket: %s                                                                                       |
| 500104  | 3S       | bind %s port %u: %s                                                                              |
| 500105  | 3S       | listen: %s                                                                                       |
| 500106  | 3S,FS,MS | fcntl O_NONBLOCK: %s                                                                             |
| 500107  | 3E       | accept: %s                                                                                       |
| 500108  | 3S       | %s: failed to start cs378x listener on %s port %u                                                |
| 500109  | MS       | socket() failed: %s                                                                              |
| 500110  | MS       | bind %s:%u failed: %s                                                                            |
| 500111  | MS       | listen failed: %s                                                                                |
| 500112  | MS       | bind %s failed: %s                                                                               |
| 500113  | LE       | rist: receiver create failed                                                                     |
| 500114  | AE       | out of memory building %s document                                                               |
| 500115  | HE       | srt output: poll failed: %s                                                                      |
| 500116  | IS       | dvbipitools: %s: %s                                                                              |
| 500117  | IS       | dvbipitools: cannot resolve own path, '%s' is not absolute                                       |
| 500118  | IS       | dvbipitools: %s: %s                                                                              |
| 500119  | LS       | cas_group: ECMG client %zu failed to start                                                       |
| 500120  | LS       | cas_group: EMMG server %zu failed to start                                                       |
| 500121  | LE       | %s: getrandom: %s                                                                                |
| 500122  | LW       | %sservice key cache full (%zu services), dropping EMM-G for service %04X                         |
| 500123  | LS       | ecmg: pthread_create: %s                                                                         |
| 500124  | LS       | emmg: socket: %s                                                                                 |
| 500125  | LS       | emmg: bind %s:%u: %s                                                                             |
| 500126  | LS       | emmg: listen: %s                                                                                 |
| 500127  | LS       | emmg: fcntl O_NONBLOCK: %s                                                                       |
| 500128  | LE,LS    | emmg: pthread_create: %s                                                                         |
| 500129  | LE       | emmg: fcntl O_NONBLOCK (slot %d): %s                                                             |
| 500130  | LE       | emmg: accept: %s                                                                                 |
| 500131  | LW       | escodec: esc_vbuf_add failed, sei nal dropped                                                    |
| 500132  | LW       | escodec: esc_vbuf_add failed, h264 nal dropped                                                   |
| 500133  | LW       | escodec: esc_vbuf_add failed, hevc nal dropped                                                   |
| 500134  | LW       | escodec: esc_vbuf_add failed, vvc nal dropped                                                    |
| 500135  | LW       | escodec: esc_vbuf_add failed, av1 obu dropped                                                    |
| 500136  | LE       | escodec: av1 obu unescape alloc failed                                                           |
| 500137  | LW       | pes: sgrow failed, pid %u payload bytes dropped                                                  |
| 500138  | LE       | fcc: socket: %s                                                                                  |
| 500139  | LE       | fcc: poll: %s                                                                                    |
| 500140  | LE       | ret: socket: %s                                                                                  |
| 500141  | LE       | ret: poll: %s                                                                                    |
| 500142  | LS       | %s: daemonize failed: %s                                                                         |
| 500143  | LS       | cannot open %s:%u for sending                                                                    |
| 500144  | LE       | httpclient: fcntl O_NONBLOCK: %s                                                                 |
| 500145  | LE       | recv: %s                                                                                         |
| 500146  | LE       | setsockopt SO_RCVBUF: %s                                                                         |
| 500147  | LW       | SO_RCVBUF capped to %d bytes (wanted %d; raise net.core.rmem_max)                                |
| 500148  | FS,LE    | bind: %s                                                                                         |
| 500149  | LE       | join %s: %s                                                                                      |
| 500150  | LE       | join %s from %s: %s                                                                              |
| 500151  | LE       | set outgoing interface: %s                                                                       |
| 500152  | LE       | set ttl: %s                                                                                      |
| 500153  | LE       | set hop limit: %s                                                                                |
| 500154  | LE       | rist: pipe/thread setup failed                                                                   |
| 500155  | LE       | rist: sender create failed                                                                       |
| 500156  | LE       | rist: UDP port %u already in use                                                                 |
| 500157  | LE       | rist: UDP port %u (RTCP of %u) already in use                                                    |
| 500158  | LE       | srt: setsockopt failed: %s                                                                       |
| 500159  | LE       | srt: socket create failed: %s                                                                    |
| 500160  | LE       | srt: epoll create failed: %s                                                                     |
| 500161  | LE       | srt: epoll add failed: %s                                                                        |
| 500162  | LE       | srt: accept wait failed: %s                                                                      |
| 500163  | LE       | srt: bind/listen failed: %s                                                                      |
| 500164  | LE       | srt: group create failed: %s                                                                     |
| 500165  | LE       | srt: bind/listen failed on peer %d: %s                                                           |
| 500166  | LE       | srt: startup failed: %s                                                                          |
| 500167  | LE       | srt output: pending buffer resize to %d chunks failed, keeping %d                                |
| 500168  | LE       | cpu-affinity: %s: sched_getaffinity: %s                                                          |
| 500169  | LE       | cpu-affinity: %s: cpu %u: %s                                                                     |
| 500170  | LE       | xmltv: out of memory reading xmltv                                                               |
| 500171  | DW       | emm cache full (%zu services), dropping EMM-G for service_id 0x%04x                              |
| 500172  | DS       | cannot start mkv/mka mux                                                                         |
| 500173  | DS       | cannot start flv mux                                                                             |
| 500174  | FE       | out of memory allocating psi state for %s:%u, RAP detection disabled for this packet             |
| 500175  | FS       | %s failed: %s                                                                                    |
| 500176  | FS       | SO_REUSEPORT: %s                                                                                 |
| 500177  | FS       | epoll_create1: %s                                                                                |
| 500178  | FS       | epoll_ctl: %s                                                                                    |
| 500179  | FA       | epoll_ctl (wake fd): %s, falling back to bounded poll                                            |
| 500180  | FW       | pacer: out of memory, burst pacing disabled                                                      |
| 500181  | FS       | %s: out of memory allocating channel table                                                       |
| 500182  | FS       | %s: out of memory allocating MC RET session table                                                |
| 500183  | FS       | %s: out of memory allocating RSI announcement table                                              |
| 500184  | FS       | %s: out of memory allocating burst table                                                         |
| 500185  | FS       | %s: out of memory allocating FCC rate limiter                                                    |
| 500186  | FS       | %s: cannot open capture: %s                                                                      |
| 500187  | FS       | %s: out of memory creating ret context                                                           |
| 500188  | FS       | %s: out of memory allocating RET rate limiter                                                    |
| 500189  | FS       | %s: failed to start listen workers on %s:%u                                                      |
| 500190  | FS       | %s: failed to start FCC resolve-by-port sockets at %s:%u..%u                                     |
| 500191  | FS       | %s: failed to start burst pacing thread                                                          |
| 500192  | FS       | %s: failed to start RSI announcement thread                                                      |
| 500193  | FS       | %s: failed to start metrics thread                                                               |
| 500194  | TS       | bitrate pacer setup failed                                                                       |
| 500195  | TW       | input %u: out of memory allocating psi state, retrying next poll                                 |
| 500196  | TE       | input %u: remux setup failed                                                                     |
| 500197  | TE       | input %u: hold-back queue setup failed                                                           |
| 500198  | TS       | hold-back queue setup failed                                                                     |
| 500199  | TS       | remux setup failed                                                                               |
| 500200  | YW       | --status-tpl reload queue full, leaking one old page                                             |
| 500201  | YW       | --status-tpl %s: reload failed (OOM), keeping previous content                                   |
| 500202  | YS       | out of memory sizing dash_lldash subscriber table (%d entries)                                   |
| 500203  | YA       | ssdp: cannot join :%d, DLNA discovery disabled                                                   |
| 500204  | YE       | ssdp: recv error: %s                                                                             |
| 500205  | YA       | ssdp: cannot open send socket, DLNA discovery disabled                                           |
| 500206  | YA       | ssdp: cannot start thread, DLNA discovery disabled                                               |
| 500207  | YS       | out of mem building channel lists                                                                |
| 500208  | YW       | conn retire queue full, leaking one conn_t                                                       |
| 500209  | YA       | out of memory sizing conn table (%d entries), fd tracking disabled                               |
| 500210  | YE       | conn table overflow, fd %d exceeds table size %d, conn_publish/conn_for_fd no-op past this point |
| 500211  | YA       | http3: reuseport steering unavailable, conn migration will only work with -j 1                   |
| 500212  | YA       | out of memory sizing fd_ssl table (%d entries), TLS not bound                                    |
| 500213  | YW       | tls reload: retire queue full, leaking one SSL_CTX                                               |
| 500214  | YW       | hls: buf_reserve failed, ts packet dropped                                                       |
| 500215  | YS       | out of memory sizing mp4push subscriber table (%d entries)                                       |
| 500216  | YW       | hls: buf_reserve failed, fmp4 access unit dropped                                                |
| 500217  | YW       | hls: buf_reserve failed, lcevc au dropped                                                        |
| 500218  | YS       | out of memory sizing segmenter table (%d entries)                                                |
| 500219  | YE       | segmenter retire alloc failed, leaking segmenter                                                 |
| 500220  | YE       | hls: snapshot retire alloc failed, leaking snapshot                                              |
| 500221  | YS       | out of memory sizing store table (%d entries)                                                    |
| 500222  | YE       | hls: close retire alloc failed, store stays open                                                 |
| 500223  | YW       | --sds %s: cannot join, list left empty                                                           |
| 500224  | YS       | out of memory sizing ts_push subscriber table (%d entries)                                       |
| 500225  | LE       | xmltv: cannot convert encoding %s to UTF-8                                                       |
| 500226  | YE       | worker %d: epoll_create1: %s                                                                     |
| 500227  | YW       | http3: udp4 bind failed, no HTTP/3 over IPv4                                                     |
| 500228  | YW       | http3: udp6 bind failed, no HTTP/3 over IPv6                                                     |
| 500229  | YW       | segmenter variant cap (%d per source) reached, rejecting new variants                            |
| 500230  | LE       | tva: cannot read input (read error or out of memory)                                             |
| 500231  | LE       | tva: out of memory parsing input                                                                 |
| 500233  | GE       | out of memory, version not advanced                                                              |
| 500235  | YA       | ssdp: IP_PKTINFO: %s, not answering searches                                                     |
| 500236  | YE       | cannot listen on %s:%u%s (worker %d): %s                                                         |
| 500237  | YW       | hls: pes_new failed, program change not applied                                                  |
| 500238  | YW       | hls: no video keyframe within buffer cap, segment data dropped                                   |
| 500240  | AE,GE    | receive on %s failed, giving up                                                                  |
| 500241  | LE       | rist: receiver start failed                                                                      |
| 500242  | LE,VE    | rist: receiver restart failed                                                                    |
| 500243  | LE       | srt: listen callback failed: %s                                                                  |
| 500244  | FE       | %s: capture poll failed: %s                                                                      |
| 500245  | FW,YW    | %s: cpu pin failed, running unpinned                                                             |
| 500246  | LE       | scrambler: batch %scrypt failed, dropping affected packets                                       |
| 500247  | DE       | input poll failed: %s                                                                            |
| 500248  | DE       | input read error: %s                                                                             |
| 500249  | LW       | pes: pid %u PES exceeds %u bytes, dropped until next start                                       |
| 500250  | BS       | cannot create temporary file                                                                     |

### 6xxxxx Remote

| Number  | Codes | Message                                                           |
|---------|-------|-------------------------------------------------------------------|
| 600100  | 3W    | %s timeout, closing (slot %d)                                     |
| 600101  | 3W    | connection limit (%d) reached, rejecting                          |
| 600102  | LE    | rist: failed to add peer: %s                                      |
| 600103  | HW,TW | srt output: link down, reconnecting                               |
| 600104  | LE    | %s: resolve %s: %s                                                |
| 600105  | LE    | %s: connect %s:%u: %s                                             |
| 600106  | LW    | ecmg: no channel_status/channel_error reply                       |
| 600107  | LE    | ecmg: channel_setup rejected, error_status=0x%04x                 |
| 600108  | LE    | ecmg: unexpected reply 0x%04x to channel_setup                    |
| 600109  | LW    | ecmg: no stream_status/stream_error reply                         |
| 600110  | LE    | ecmg: stream_setup rejected, reply=0x%04x error_status=0x%04x     |
| 600111  | LW    | ecmg: no ECM_response for CP %u                                   |
| 600112  | LW    | ecmg: no channel_status reply to channel_test                     |
| 600113  | LE    | ecmg: async error 0x%04x, error_status=0x%04x                     |
| 600114  | LE    | ecmg: CW_provision rejected, reply=0x%04x error_status=0x%04x     |
| 600115  | LW    | ecmg: connection lost                                             |
| 600116  | LW    | emmg: client reported error (message 0x%04x, error_status 0x%04x) |
| 600117  | LW    | emmg: connection closed by peer or read error (slot %d)           |
| 600118  | LW    | emmg: connection limit (%u) reached, rejecting                    |
| 600119  | LE    | fcc: burst request rejected, response %u                          |
| 600120  | LE    | fcc: bad server address: %s                                       |
| 600121  | LE    | fcc: connect: %s                                                  |
| 600122  | LE    | ret: connect: %s                                                  |
| 600123  | LE    | http: too many redirects                                          |
| 600124  | LE    | http: redirect without usable Location                            |
| 600125  | LE    | http %d fetching %s%s                                             |
| 600126  | LE    | http: response headers too large                                  |
| 600127  | LE    | http: connection closed while reading headers                     |
| 600128  | LE    | connect %s:%u: %s                                                 |
| 600129  | LE    | send: timed out waiting for socket readiness                      |
| 600130  | LE    | http: timed out waiting for response headers                      |
| 600131  | LE    | resolve %s: %s                                                    |
| 600132  | LW    | connect failed, retrying in %lds                                  |
| 600133  | LW    | connect failed, not retrying                                      |
| 600134  | LW    | rtmpout: %s: send queue over %u bytes, reconnecting               |
| 600135  | LE    | srt: address resolve failed for %s:%u: %s                         |
| 600136  | LE    | srt: rendezvous failed: %s                                        |
| 600137  | LE    | srt: connect failed: %s                                           |
| 600138  | LE    | srt: accept failed: %s                                            |
| 600139  | LE    | srt: group connect failed: %s                                     |
| 600140  | LE    | srt: accept_bond failed: %s                                       |
| 600141  | LW    | srt: reconnect attempt failed: %s                                 |
| 600142  | LW    | srt: read failed on group (%s), reconnecting                      |
| 600143  | LW    | srt: link lost (%s), reconnecting                                 |
| 600145  | LW    | srt: %s, reconnecting                                             |
| 600146  | DE    | -u/--unicast-emm fetch timed out                                  |
| 600147  | DW    | -u/--unicast-emm response too large, truncated                    |
| 600148  | CW,DW | srt[%d] output: link down, reconnecting                           |
| 600149  | RE    | input %u (%s): open timed out after %.0fs                         |
| 600150  | RW    | srt output: link down, reconnecting                               |
| 600151  | TW    | input %u: source PMT changed incompatibly, reconnecting           |
| 600152  | TW    | source PMT changed incompatibly, reconnecting                     |
| 600153  | YA    | ret server %s:%u unreachable                                      |
| 600154  | YA    | al-fec repair stream %s:%u unreachable                            |
| 600155  | YA    | fcc server %s:%u unreachable                                      |
| 600156  | YE    | srt source %s:%u: connect failed, source left unavailable         |
| 600157  | YE    | http source %s: %s, source left unavailable                       |
| 600158  | YW    | --join-all: %s unreachable, left unjoined                         |
| 600159  | YW    | --sds %s: no services discovered in %.0fs                         |
| 600160  | LW    | rtmpout: %s: setup timed out after %us, reconnecting              |
| 600161  | LE    | http: Location header longer than %zu bytes                       |
| 600162  | LW    | rist: no peer connected, data may be lost                         |
| 600163  | VW    | http source lost: %s, reconnecting                                |
| 600164  | VS    | http source: no response within %.0fs                             |
| 600165  | LW    | srt: group member re-add failed: %s                               |
| 600166  | LE    | http: %s %s:%u timed out after %ds                                |
| 600167  | LE    | http: body read stalled for %ds                                   |
| 600168  | LE    | http: body read failed after %zu bytes                            |

### 7xxxxx Misc

| Number  | Codes     | Message                                                                                      |
|---------|-----------|----------------------------------------------------------------------------------------------|
| 700001  | LA,LS     | %s: %u warnings or errors in config (summary line)                                           |
| 700010  | LD,LE,LW  | rist: %s                                                                                     |
| 700011  | LD,LE,LW  | srt: %s                                                                                      |
| 700100  | GD        | segment %u: %d channels, %d programmes, %d fragments                                         |
| 700101  | BD        | %d channels, %d programmes, %d fragments -> %zu+%zu bytes                                    |
| 700102  | BD        | %d channels, %d programmes, %d fragments read                                                |
| 700103  | 3D        | no CW available yet, dropping request                                                        |
| 700104  | MD        | 404 %s %s                                                                                    |
| 700105  | MW        | SIGUSR1: no TLS configured, ignoring                                                         |
| 700106  | VA        | warning: this librist build (<=0.2.20) has known IPv6 handling bugs, %s may crash it         |
| 700107  | AW        | cannot keep segment %u, dropped                                                              |
| 700108  | AW        | segment %u: %d services, more present beyond the %d cap                                      |
| 700109  | AD        | segment %u: %d service%s                                                                     |
| 700110  | IA        | dvbipitools: %s: exists, skipped                                                             |
| 700111  | LA        | %scas: %u vendors configured, only the first %d will be started                              |
| 700112  | LE        | cas_group: no clock activity observed within %.0fs, giving up                                |
| 700113  | LW        | ecmg: --cas-cp-duration %ums is below the ECMG's min_CP_duration %ums, using it anyway       |
| 700114  | LW        | emmg: EMM queue full, dropping oldest datagram                                               |
| 700115  | LD        | emmg: rx version=0x%02x type=0x%04x body_len=%zu                                             |
| 700116  | LD        | emmg: data_provision, %d datagram(s) queued                                                  |
| 700117  | LD        | emmg: tx version=0x%02x type=0x%04x                                                          |
| 700118  | LW        | ret: outq full (%d slots), dropping packet - accounting invariant violated                   |
| 700119  | LW        | ret: seq %u lost, not repaired in time                                                       |
| 700120  | LW        | ret: seq %u lost, gap abandoned                                                              |
| 700121  | LW        | ret: payload %zu exceeds hold cap (%d), dropping until it fits again                         |
| 700122  | LE        | ret: ssrc changed (0x%08x -> 0x%08x), resetting gap tracking                                 |
| 700123  | LW        | ret: repair payload %zu exceeds hold cap (%d), dropping until it fits again                  |
| 700124  | LW        | HLS packed audio is not ADTS AAC, skipped                                                    |
| 700125  | LW        | metrics: label for id %u truncated to %d bytes: %s                                           |
| 700126  | LD        | dropped snapshot from new instance %s/%s, store full (%d slots)                              |
| 700127  | LW        | mp4: index outgrew reserved region, no further crash checkpoints                             |
| 700128  | LW        | dvbstp: reassembly slots full (%d), evicting in-progress payload_id=0x%02x segment_id=%u     |
| 700129  | LA        | rist: this build has no librist support, rist:// input unavailable                           |
| 700130  | LA        | rist: this build has no librist support, rist:// output unavailable                          |
| 700131  | LA        | srt: this build has no libsrt support, srt:// output unavailable                             |
| 700132  | LA        | srt: this build has no libsrt support, srt:// input unavailable                              |
| 700133  | LE        | %s failed: %s                                                                                |
| 700134  | LW        | jitter buffer: reordered %llu lost %llu late %llu dup %llu resync %llu dropped %llu          |
| 700135  | FW        | max-channels (%zu) reached, rejecting %s:%u                                                  |
| 700136  | FW        | max-bursts (%zu) reached, rejecting new burst session                                        |
| 700137  | FW        | mcsend table full, dropping RET multicast for a channel - accounting invariant violated      |
| 700138  | FW        | NACK repairs rate-limited, %llu so far                                                       |
| 700139  | FW        | RAMS-R rate-limited, %llu so far                                                             |
| 700140  | FA        | RSI self-announcement needs an IPv4 -l address, disabling it                                 |
| 700141  | TW        | cas: pid 0x%x dropped, already at the %zu pid cap                                            |
| 700142  | TW        | bitrate: content exceeds -b target by >%.0fs of backlog, not corrected (pass -B to throttle) |
| 700143  | TW        | program %u: ES cap (%d) reached, dropping %d stream%s                                        |
| 700144  | YW        | hls: hls_set_init_segment failed, init segment lost                                          |
| 700145  | YW        | hls: hls_push_part failed, fmp4 chunk lost                                                   |
| 700146  | YW        | hls: hls_push_segment_ll failed, fmp4 segment lost                                           |
| 700147  | YW        | hls: hls_push_segment failed, fmp4 segment lost                                              |
| 700148  | YW        | hls: hls_push_part failed, part lost                                                         |
| 700149  | YW        | hls: hls_push_segment_ll failed, segment lost                                                |
| 700150  | YW        | hls: hls_push_segment failed, segment lost                                                   |
| 700151  | YW        | ts_push: h2 ring full, resetting stream                                                      |
| 700152  | YW        | ts_push: h3 ring full, resetting stream                                                      |
| 700153  | YW        | hls: discontinuity not recorded, old segments stay listed                                    |
| 700154  | YW        | hls: program layout changed, relocked on video pid 0x%04x                                    |
| 700155  | YW        | hls: store anchor failed, availabilityStartTime stays at store open                          |
| 700156  | YW        | hls: video track store sync failed, stale segments stay listed                               |
| 700157  | YW        | hls: audio track store sync failed, stale segments stay listed                               |
| 700158  | YW        | hls: track init segment lost, DASH falls back to muxed                                       |
| 700159  | YW        | hls: track store push failed, DASH track data lost                                           |
| 700160  | YW        | hls: audio parameters not recorded, DASH audio rate and channels missing                     |
| 700161  | YW        | hls: video timestamp jump, segmenter restarted on video pid 0x%04x                           |
| 700163  | LW        | mapping: duplicate id %s, first entry wins                                                   |
| 700164  | MD        | 405 %s %s                                                                                    |
| 700165  | TW        | program %u: PES header split across packets, timestamps not shifted                          |
| 700166  | TW        | program %u: time offset re-latched after PCR or timestamp jump                               |
| 700167  | TW        | program %u: hold queue full, packet released early                                           |
| 700168  | TW        | program %u: source continuity error on pid 0x%04x, cc %u after %u                            |

### 8xxxxx and 9xxxxx

reserved
