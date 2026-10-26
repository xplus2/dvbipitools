/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_CODEC_H
#define DVBIPITOOLS_LIB_BIM_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "bimreader.h"
#include "bitwriter.h"
#include "strrepo.h"

#ifdef __clang__
#define BIM_TAKES_ARG(n) __attribute__((ownership_takes(malloc, n)))
#else
#define BIM_TAKES_ARG(n)
#endif

#define XSI_NS "http://www.w3.org/2001/XMLSchema-instance"
#define XML_NS "http://www.w3.org/XML/1998/namespace"

/* dvbStringCodec, TS 102 323 9.4.3.3 */
int dvb_string_encode(strrepo_writer_t *sw, const char *s);
int dvb_string_decode(strrepo_reader_t *sr, char *out, size_t outcap);
/* heap variant, never truncated. empty string -> *out NULL, old *out freed. 0 ok, -1 error */
int dvb_string_decode_dup(strrepo_reader_t *sr, char **out);

/* classification schemes for dvbControlledTermCodec, TS 102 323 9.4.3.7.
   rank: termID of term. term: inverse, both 0 ok.
   list: ClassificationSchemeURIs of group 0, Table 76 */
typedef struct {
  int (*rank)(void *ctx, const char *cs_uri, const char *term, uint64_t *rank);
  int (*term)(void *ctx, const char *cs_uri, uint64_t rank, char *out, size_t cap);
  void *ctx;
  const char *const *list;
  size_t nlist;
  int compact; /* encode as scheme + term id where known */
} dvb_cs_t;

/* reuse state of one fragment: locator prefix and day, dateTime date */
typedef struct {
  int have_prefix;
  unsigned onid;
  unsigned tsid;
  unsigned sid;
  int have_day;
  unsigned day;
  int have_date;
  long mjd;
  const dvb_cs_t *cs;
} dvb_codec_state_t;

/* dvbLocatorCodec, TS 102 323 9.4.3.4. encode: string branch only */
int dvb_locator_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *uri);
/* optimized: OptimizedDVBLocator if lossless, st: prefix and day reuse. else: str */
int dvb_locator_encode_st(bitwriter_t *bw, strrepo_writer_t *sw, dvb_codec_state_t *st, const char *uri, int optimized);
int dvb_locator_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap);
/* also optimized branch, rendered as dvb:// text */
int dvb_locator_decode_st(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, char *out, size_t outcap);

/* dvbDateTimeCodec. encode: dateTime_flag==00, full precision. decode also PublishedTime (minute precision) */
int dvb_datetime_encode(bitwriter_t *bw, const char *iso8601);
/* published: PublishedTime if seconds 0 and date in 16 bit MJD window, st: date reuse. Else as above */
int dvb_datetime_encode_st(bitwriter_t *bw, dvb_codec_state_t *st, const char *iso8601, int published);
int dvb_datetime_decode(bitreader_t *br, char *out, size_t outcap);
int dvb_datetime_decode_st(bitreader_t *br, dvb_codec_state_t *st, char *out, size_t outcap);

/* TS 102 822-3-2 4.4.2.4.2, 4.4.2.4.3: MJD and ms of day in UTC, MJD. encode: 0 ok, -1 not representable */
int tva_datetime_encode(bitwriter_t *bw, const char *v);
int tva_datetime_decode(bitreader_t *br, char *out, size_t cap);
int tva_date_encode(bitwriter_t *bw, const char *v);
int tva_date_decode(bitreader_t *br, char *out, size_t cap);

/* dvbDurationCodec, TS 102 323 9.4.3.6. compact: minutes form for exact PTnHnM, else string */
int dvb_duration_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *s, int compact);

/* dvbControlledTermCodec, TS 102 323 9.4.3.7. without cs: encode as string, decode renders compact form as dvbcs:group:scheme:term */
int dvb_controlledterm_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *href);
int dvb_controlledterm_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap);
/* cs: compact form table 70 schemes with known term, else str. decode: URI:term if cs known */
int dvb_controlledterm_encode_cs(bitwriter_t *bw, strrepo_writer_t *sw, const dvb_cs_t *cs, const char *href);
int dvb_controlledterm_decode_cs(bitreader_t *br, strrepo_reader_t *sr, const dvb_cs_t *cs, char *out, size_t outcap);

/* schema-driven BiM, ISO/IEC 15938-1:2002 + ISO/IEC 23001-1. tree <-> bits for 1 fragment payload */

/* simple type codecs. ISO 23001-1 7.5.4 defaults, DVB TS 102 323 9.4.3, TVA TS 102 822-3-2 4.4.2.4 */
typedef enum {
  BIM_UTF8, /* vluimsbf5 byte count + UTF-8, ISO 23001-1 7.5.4.1 */
  BIM_BOOLEAN,
  BIM_FLOAT, /* IEEE 754 single */
  BIM_DOUBLE, /* IEEE 754 double */
  BIM_HEXBIN, /* vluimsbf5 bit count + bits, text as hex */
  BIM_BASE64, /* vluimsbf5 bit count + bits, text as base64 */
  BIM_RANGE, /* integer bounded both sides, 7.5.4.2.1 */
  BIM_SVLU5, /* sign bit + vluimsbf5 abs value, integer not bounded both sides */
  BIM_ENUM, /* index into sorted values, 7.5.4.2.2 */
  BIM_LIST, /* length + items, 7.5.4.2.3 */
  BIM_UNION, /* member code + member value, 7.5.4.2.4 */
  BIM_STRING, /* dvbStringCodec */
  BIM_LOCATOR, /* dvbLocatorCodec */
  BIM_DATETIME, /* dvbDateTimeCodec */
  BIM_PUBTIME, /* dvbDateTimeCodec, PublishedTime form preferred (TS 102 323 9.4.3.5) */
  BIM_DURATION, /* dvbDurationCodec */
  BIM_TERMREF, /* dvbControlledTermCodec */
  BIM_ZSTRING, /* TVA Zlib string codec */
  BIM_M7_TIMEPOINT, /* basicTimePointType, ISO 15938-1:2002 8.5.4.2.5 */
  BIM_M7_DURATION, /* basicDurationType, ISO 15938-1:2002 8.5.4.2.6 */
  BIM_TVA_DATETIME, /* tvadateTimeCodec, TS 102 822-3-2 4.4.2.4.2 */
  BIM_TVA_DATE, /* tvadateCodec, TS 102 822-3-2 4.4.2.4.3 */
  BIM_UNSUPPORTED
} bim_codec_t;

/* inclusive bounds */
typedef struct {
  int64_t min;
  uint64_t span; /* max-min */
} bim_range_t;

typedef struct bim_simple bim_simple_t;

struct bim_simple {
  bim_codec_t codec;
  const char *const *enums; /* BIM_ENUM, sorted */
  int nenums;
  bim_range_t range; /* BIM_RANGE value, BIM_LIST length */
  int unbounded; /* BIM_LIST: no maxLength */
  const bim_simple_t *item; /* BIM_LIST */
  const bim_simple_t *base; /* restricted type, NULL none */
  const bim_simple_t *const *members; /* BIM_UNION, definition order */
  int nmembers;
  /* facets deciding union membership, XML Schema Part 2 2.5.1.3: one pattern per derivation step,
     match 1 if v matches, length in characters (octets for binary) */
  const void *const *patterns;
  int npatterns;
  int (*match)(const void *pattern, const char *v);
  int has_length;
  uint64_t minlen;
  uint64_t maxlen;
};

typedef struct {
  const char *name; /* local name */
  const char *ns; /* NULL none */
  int required;
  const bim_simple_t *simple;
} bim_attr_t;

typedef enum { BIM_WC_ANY, BIM_WC_NOT, BIM_WC_LIST } bim_wc_kind_t;
typedef enum { BIM_PC_STRICT, BIM_PC_LAX, BIM_PC_SKIP } bim_process_t;

/* namespace constraint. NOT: all but ns[0], LIST: ns[]. NULL entry: absent */
typedef struct {
  bim_wc_kind_t kind;
  const char *const *ns;
  int nns;
  bim_process_t process;
} bim_wildcard_t;

typedef enum { BIM_CONTENT_NONE, BIM_CONTENT_SIMPLE, BIM_CONTENT_SEQ } bim_content_t;
typedef enum { BIM_PART_ELEM, BIM_PART_SEQ, BIM_PART_CHOICE, BIM_PART_ALL, BIM_PART_ANY } bim_part_kind_t;

typedef struct bim_type bim_type_t;
typedef struct bim_elem bim_elem_t;

/* global element. substs: substitution group members in SubstitutionSelect order, ISO 23001-1 6.6.5.3 */
struct bim_elem {
  const char *name;
  const char *ns;
  const bim_type_t *type;
  int nillable;
  int abstract;
  const bim_elem_t *const *substs;
  int nsubsts;
};

typedef struct bim_part {
  bim_part_kind_t kind;
  const char *name; /* ELEM local name */
  const char *ns; /* ELEM namespace, NULL none */
  const bim_type_t *type; /* ELEM, NULL = not decodable */
  uint64_t min;
  uint64_t max; /* BIM_UNBOUNDED for unbounded */
  const struct bim_part *kids;
  int nkids;
  int nillable; /* ELEM */
  const bim_elem_t *elem; /* ELEM referencing a global element, NULL local */
  const bim_wildcard_t *wc; /* ANY */
} bim_part_t;

#define BIM_UNBOUNDED UINT64_MAX

/* subtypes of base in type code order (ISO 15938-1 7.6.5.4), NULL: no table.
   deltas: tables of later chunks (8.4.4), NULL: none */
typedef struct {
  const bim_type_t *base;
  const bim_type_t *const *subs;
  int nsubs;
  const bim_type_t *const *deltas;
} bim_subtypes_t;

/* one SchemaURI. known 0: its chunks skipped. elems: global elements in selector order (ISO 23001-1 6.6.5.2.3),
   types: named types in type code order (6.6.5.4), attrs: global attributes by expanded name (7.5.3.3) */
typedef struct {
  const bim_subtypes_t *subs;
  int nsubs;
  int known;
  const struct bim_elem *const *elems;
  int nelems;
  const struct bim_type *const *types;
  int ntypes;
  const bim_attr_t *attrs;
  int nattrs;
  const bim_simple_t *string; /* xs:string, mixed character data */
} bim_schema_t;

/* subtype table of base t in sc, NULL none */
const bim_subtypes_t *bim_subtypes_find(const bim_schema_t *sc, const bim_type_t *t);
/* as find, then base of same expanded name. Schemas compile imports separately */
const bim_subtypes_t *bim_subtypes_find_x(const bim_schema_t *sc, const bim_type_t *t);

typedef enum { BIM_SBC_ELEM, BIM_SBC_SIMPLE, BIM_SBC_ANY, BIM_SBC_ATTR, BIM_SBC_ANYATTR } bim_sbc_kind_t;

/* child of a complex type for Schema Branch Codes, ISO 23001-1 6.6.5.2.2 */
typedef struct {
  bim_sbc_kind_t kind;
  const char *name; /* ELEM, ATTR */
  const char *ns;
  const bim_type_t *type; /* ELEM */
  const bim_simple_t *simple; /* SIMPLE, ATTR */
  const bim_wildcard_t *wc; /* ANY, ANYATTR */
} bim_sbc_t;

/* attrs sorted in coding order. top: NULL unless content is BIM_CONTENT_SEQ. name NULL: anonymous */
struct bim_type {
  const bim_attr_t *attrs;
  int nattrs;
  bim_content_t content;
  const bim_simple_t *simple; /* BIM_CONTENT_SIMPLE */
  const bim_part_t *top;
  const char *name;
  const char *ns;
  const bim_type_t *base;
  int abstract;
  int mixed;
  const bim_wildcard_t *anyattr; /* NULL none */
  const bim_sbc_t *sbc; /* SBC order */
  int nsbc;
  const bim_attr_t *fixed; /* declared fixed, not coded */
  int nfixed;
};

/* xmlns declaration, prefix "" default */
typedef struct {
  const char *prefix;
  const char *uri;
} bim_nsdecl_t;

/* name NULL: mixed content text node. non-heap strings schema- or caller-owned */
typedef struct bim_node {
  const char *name; /* local name */
  const char *ns; /* namespace URI, NULL none */
  char *text; /* simple content, heap */
  struct {
    const char *name;
    const char *ns;
    char *value; /* heap */
  } *attrs;
  int nattrs;
  struct bim_node **kids;
  int nkids;
  int kcap;
  struct bim_node *parent; /* set by bim_node_add */
  bim_nsdecl_t *nsdecls; /* heap array */
  int nnsdecls;
  const struct bim_type *cast; /* xsi:type, NULL none */
  uint64_t pos; /* binary format document tree position (ISO 23001-1 6.6.5.5) */
  int pos_set;
  int deferred; /* ISO 23001-1 5.7.2 */
  char *href; /* fragment reference, heap */
  char **pool; /* strings owned for names below, heap */
  int npool;
} bim_node_t;

bim_node_t *bim_node_new(const char *name);
void bim_node_free(bim_node_t *n);
int bim_tree_has_cast(const bim_node_t *n);
/* copies value, replaces an existing one. 0 ok, -1 oom */
int bim_node_set_attr(bim_node_t *n, const char *name, const char *value);
/* takes value, also on failure. 0 ok, -1 oom, -2 duplicate ns+name */
int bim_node_add_attr_ns(bim_node_t *n, const char *ns, const char *name, char *value) BIM_TAKES_ARG(4);
const char *bim_node_attr(const bim_node_t *n, const char *name);
/* ns NULL: no namespace */
const char *bim_node_attr_ns(const bim_node_t *n, const char *ns, const char *name);
/* element node with expanded name ns, name */
int bim_node_is(const bim_node_t *n, const char *ns, const char *name);
/* URI bound to prefix ("" default) at n or its ancestors, xml prefix built in. NULL unbound */
const char *bim_node_ns_uri(const bim_node_t *n, const char *prefix);
/* qn in scope of n to ns (NULL none), local. -1 malformed, -2 unbound prefix */
int bim_node_qname(const bim_node_t *n, const char *qn, const char **ns, const char **local);
/* copy of s owned by n, NULL on oom */
const char *bim_node_keep(bim_node_t *n, const char *s);
/* copies text, NULL clears. 0 ok, -1 oom */
int bim_node_set_text(bim_node_t *n, const char *text);
/* appends child (takes ownership on success). 0 ok, -1 oom */
int bim_node_add(bim_node_t *n, bim_node_t *kid);
/* new child with name, set text when non-NULL. NULL on oom */
bim_node_t *bim_node_add_new(bim_node_t *n, const char *name, const char *text);
/* first child with name or NULL */
const bim_node_t *bim_node_child(const bim_node_t *n, const char *name);

/* bim_encode_payload_opts with defaults (len_mode 0, mpegb as given) */
int bim_encode_payload(const bim_type_t *t, const bim_node_t *root, int mpegb, bitwriter_t *bw, strrepo_writer_t *sw);

/* zlib uncompress, 0 ok */
typedef int (*bim_inflate_fn)(const unsigned char *in, size_t inlen, unsigned char *out, size_t *outlen);

/* ISO 23001-1 5.7.1: payload of a non-deferred fragment reference, *out mallocd. 0 ok */
typedef int (*bim_fragref_fn)(void *ctx, const char *href, unsigned format, unsigned char **out, size_t *outlen);
/* 1 if element or type can be substituted or cast in another schema (6.7.5.4, 6.7.6.4) */
typedef int (*bim_external_fn)(void *ctx, const bim_elem_t *e);

/* advanced optimised decoders, ISO 23001-1 clause 8 */
typedef enum { BIM_AOD_ZLIB = 1, BIM_AOD_UQ, BIM_AOD_NUQ, BIM_AOD_QNAME } bim_aod_kind_t;

typedef struct {
  bim_aod_kind_t kind;
  double vmin;
  double vmax;
  int nbits; /* UQ: nbits, NUQ: nbitsCharacteristics */
  int nbits_data;
  int nchar;
  uint32_t *matrix; /* [nchar][2^nbits_data] */
  int counter; /* NUQ characteristic of next value (8.6.3.3) */
} bim_aod_inst_t;

/* decoders for simple and its derived types. preserve: default decoder first */
typedef struct {
  const bim_simple_t *simple;
  int preserve;
  const int *inst;
  int ninst;
  int *heap_inst;
} bim_aod_map_t;

typedef struct {
  bim_aod_inst_t *insts;
  int ninsts;
  bim_aod_map_t *maps;
  int nmaps;
} bim_aod_t;

/* nearest mapping of s or its bases, NULL none */
const bim_aod_map_t *bim_aod_find(const bim_aod_t *a, const bim_simple_t *s);

/* MPEG-7 Systems conformance stream deviations from standard text, off by default */
#define BIM_COMPAT_NO_SCHEMA_MODE 1u /* no SchemaModeUpdate with several schemas */
#define BIM_COMPAT_M7_FLAGS 2u /* reference software time points, durations: named types, no flags for excluded sections, sign flag first, month and day 0-based. Compile option */
#define BIM_COMPAT_ATTR_LOCAL 4u /* attributes ordered by local name, compile option */
#define BIM_COMPAT_FIXED_ATTR 8u /* optional fixed attributes coded in 15938-1:2002 syntax, compile option */
#define BIM_COMPAT_LIST_STRING 16u /* built-in list types (IDREFS, NMTOKENS, ENTITIES) as strings, compile option */

typedef struct {
  const bim_schema_t *schemas; /* [0]: payload type's. NULL: one schema, no subtypes */
  int nschemas; /* NumberOfSchemas + NumberOfAdditionalSchemas */
  bim_inflate_fn inflate; /* zlib codecs, NULL: unsupported */
  size_t *zleft; /* non-NULL: undelivered decompressed bytes */
  int mpegb; /* 23001-1:2006 payload syntax, 0: 15938-1:2002 */
  unsigned compat; /* BIM_COMPAT_* */
  int lax_schema; /* SchemaID of urn:mpeg:mpegb:bim:laxencoding:2007, -1 none */
  const unsigned char *frag_formats; /* SupportedFragmentReferenceFormat, [0] default */
  int nfrag_formats;
  bim_fragref_fn resolve;
  void *ctx;
  bim_external_fn ext_subst;
  bim_aod_t *aod; /* NULL: no advanced optimised decoders, instance counters advance */
  const char *const *schema_prefix; /* prefix per SchemaID for QNameExpression, NULL entry: keep numeric */
  const dvb_cs_t *cs; /* NULL: controlled terms as dvbcs:group:scheme:term */
  size_t zbuf_size; /* Zlib BufferSize, 0: 1000 bytes (TS 102 822-3-2 4.4.1) */
} bim_decode_opts_t;

/* zlib compress, 0 ok */
typedef int (*bim_deflate_fn)(const unsigned char *in, size_t inlen, unsigned char *out, size_t *outlen);

/* publisher choices, ISO 23001-1 7.3.2, 7.4. Decoder side: DecodingModes and decode opts */
typedef struct {
  const bim_schema_t *schemas; /* [0]: payload type's. NULL: one schema, no subtypes */
  int nschemas;
  int mpegb; /* 23001-1:2006 payload syntax, 0: 15938-1:2002 */
  unsigned compat; /* BIM_COMPAT_* */
  const char *const *schema_uris; /* SchemaURI[1..] a stream writer lists in its DecoderInit */
  size_t nschema_uris;
  int len_mode; /* lengthCodingMode: 0 none, 1 flag per element (set for complex content), 2 always */
  int deferred; /* hasDeferredNodes */
  int cast; /* hasTypeCasting */
  int fragref; /* hasFragmentReference: every element is a reference */
  int multi; /* 0 mono, 1 multi version where cast needs it, 2 always (nschemas > 1) */
  int freeze; /* SchemaModeStatus frozen below top element (nschemas > 1) */
  int lax_schema; /* SchemaID of urn:mpeg:mpegb:bim:laxencoding:2007, -1 none */
  int lax_zlib; /* lax events zlib compressed */
  int lax_agg; /* lax symbols agglutinated */
  unsigned char frag_format; /* fragment reference format, 1: URI */
  int aod_plain; /* preserved default decoder preferred over mapped instance */
  bim_external_fn ext_subst;
  void *ctx;
  bim_aod_t *aod;
  bim_deflate_fn deflate; /* zlib codecs, NULL: unsupported */
  const char *const *schema_prefix; /* prefix per SchemaID for QNameExpression */
  const dvb_cs_t *cs; /* NULL: controlled terms as strings */
  int dvb_locator; /* optimized locators where representable */
  int dvb_duration; /* durations in minutes where exact */
  int dvb_pubtime; /* PublishedTime form of dateTime where representable */
  size_t zbuf_size; /* Zlib BufferSize, 0: 1000 bytes */
  int zgather; /* gather strings up to BufferSize per chunk. 0: chunk per string */
  char *diag; /* failure detail, NULL none */
  size_t ndiag;
} bim_encode_opts_t;

/* FragmentUpdatePayload of root. sw: string repository (required), o NULL: one schema, mono. 0 ok, -1 error */
int bim_encode_payload_opts(const bim_type_t *t, const bim_node_t *root, const bim_encode_opts_t *o, bitwriter_t *bw, strrepo_writer_t *sw);
/* SimpleType() payload, ISO 23001-1 7.3.1 */
int bim_encode_simple(const bim_simple_t *s, const char *v, const bim_encode_opts_t *o, bitwriter_t *bw, strrepo_writer_t *sw);

/* *out: mallocd tree named rootname. opts, sr NULL ok. deferred nodes dropped
   -1 malformed, unsupported or oom, *out untouched */
int bim_decode_payload(const bim_type_t *t, const char *rootname, const bim_decode_opts_t *opts, bitreader_t *br, strrepo_reader_t *sr,
                       bim_node_t **out);
/* xs:string default decoder, mixed character data (7.5.2.4.4) */
extern const bim_simple_t bim_xs_string;
/* SimpleType() payload, ISO 23001-1 7.3.1. *out mallocd */
int bim_decode_simple(const bim_simple_t *s, const bim_decode_opts_t *opts, bitreader_t *br, strrepo_reader_t *sr, char **out);

#endif
