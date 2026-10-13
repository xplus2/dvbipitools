/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_CODEC_H
#define DVBIPITOOLS_LIB_BIM_CODEC_H

#include <stddef.h>

#include "bimreader.h"
#include "bitwriter.h"
#include "strrepo.h"

/* dvbStringCodec, TS 102 323 9.4.3.3 */
int dvb_string_encode(strrepo_writer_t *sw, const char *s);
int dvb_string_decode(strrepo_reader_t *sr, char *out, size_t outcap);
/* heap variant, never truncated. empty string -> *out NULL, old *out freed. 0 ok, -1 error */
int dvb_string_decode_dup(strrepo_reader_t *sr, char **out);

/* reuse state of one fragment: locator prefix and day, dateTime date */
typedef struct {
  int have_prefix;
  unsigned onid, tsid, sid;
  int have_day;
  unsigned day;
  int have_date;
  long mjd;
} dvb_codec_state_t;

/* dvbLocatorCodec, TS 102 323 9.4.3.4. encode: string branch only */
int dvb_locator_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *uri);
int dvb_locator_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap);
/* as decode, also optimized locator branch (rendered as dvb:// text) with prefix and day reuse */
int dvb_locator_decode_st(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, char *out, size_t outcap);

/* dvbDateTimeCodec. encode: dateTime_flag==00, full precision. decode also PublishedTime (minute precision) */
int dvb_datetime_encode(bitwriter_t *bw, const char *iso8601);
int dvb_datetime_decode(bitreader_t *br, char *out, size_t outcap);
int dvb_datetime_decode_st(bitreader_t *br, dvb_codec_state_t *st, char *out, size_t outcap);

/* dvbControlledTermCodec, TS 102 323 9.4.3.7. decode renders the compact form as dvbcs:group:scheme:term */
int dvb_controlledterm_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *href);
int dvb_controlledterm_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap);

/* Schema-driven BiM (ISO/IEC 15938-1) subset in the DVB profile of TS 102 323 9.4:
   DecodingModes all zero, no type casting, no deferred nodes, no element lengths.
   Tree <-> bits for one fragment payload. */

typedef enum {
  BIM_STRING,
  BIM_LOCATOR,
  BIM_DATETIME,
  BIM_DURATION,
  BIM_TERMREF,
  BIM_BOOLEAN,
  BIM_UVLU8,
  BIM_ENUM,
  BIM_UNSUPPORTED
} bim_codec_t;

typedef struct {
  const char *name; /* "xml:lang" for the xml namespace */
  bim_codec_t codec;
  int required;
  const char *const *enums;
  int nenums;
} bim_attr_t;

typedef enum { BIM_CONTENT_NONE, BIM_CONTENT_SIMPLE, BIM_CONTENT_SEQ } bim_content_t;
typedef enum { BIM_PART_ELEM, BIM_PART_SEQ, BIM_PART_CHOICE, BIM_PART_ANY } bim_part_kind_t;

typedef struct bim_type bim_type_t;

typedef struct bim_part {
  bim_part_kind_t kind;
  const char *name; /* ELEM only */
  const bim_type_t *type; /* ELEM only, NULL = not decodable */
  unsigned min;
  unsigned max; /* BIM_UNBOUNDED for unbounded */
  const struct bim_part *kids;
  int nkids;
} bim_part_t;

#define BIM_UNBOUNDED 65535u

/* attrs sorted in coding order. top: NULL unless content is BIM_CONTENT_SEQ */
struct bim_type {
  const bim_attr_t *attrs;
  int nattrs;
  bim_content_t content;
  bim_codec_t codec; /* BIM_CONTENT_SIMPLE */
  const bim_part_t *top;
};

typedef struct bim_node {
  const char *name; /* schema-owned or caller-owned static string, never freed */
  char *text; /* simple content, heap */
  struct {
    const char *name; /* schema-owned */
    char *value; /* heap */
  } *attrs;
  int nattrs;
  struct bim_node **kids;
  int nkids;
  int kcap;
} bim_node_t;

bim_node_t *bim_node_new(const char *name);
void bim_node_free(bim_node_t *n);
/* copies value, replaces an existing one. 0 ok, -1 oom */
int bim_node_set_attr(bim_node_t *n, const char *name, const char *value);
const char *bim_node_attr(const bim_node_t *n, const char *name);
/* copies text, NULL clears. 0 ok, -1 oom */
int bim_node_set_text(bim_node_t *n, const char *text);
/* appends child (takes ownership on success). 0 ok, -1 oom */
int bim_node_add(bim_node_t *n, bim_node_t *kid);
/* new child with name, set text when non-NULL. NULL on oom */
bim_node_t *bim_node_add_new(bim_node_t *n, const char *name, const char *text);
/* first child with name or NULL */
const bim_node_t *bim_node_child(const bim_node_t *n, const char *name);

/* FragmentUpdatePayload for root of type t. Strings go to sw. 0 ok, -1 error */
int bim_encode_payload(const bim_type_t *t, const bim_node_t *root, bitwriter_t *bw, strrepo_writer_t *sw);

/* *out malloc'd tree named rootname (caller bim_node_free). -1 on malformed, unsupported or oom, *out untouched */
int bim_decode_payload(const bim_type_t *t, const char *rootname, bitreader_t *br, strrepo_reader_t *sr, bim_node_t **out);

#endif
