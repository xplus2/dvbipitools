/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_CODEC_DECODE_PRIV_H
#define DVBIPITOOLS_LIB_BIM_CODEC_DECODE_PRIV_H

#include "lib/bim/codec.h"
#include "lib/bim/aod.h"
#include "lib/bim/codec/codec_int.h"

#define ELEM_DEPTH_MAX 256 /* element nesting, as XML parser */

typedef struct {
  bitreader_t *br;
  strrepo_reader_t *sr;
  dvb_codec_state_t st;
  int nodes;
  int len_mode; /* lengthCodingMode */
  int deferred;
  int cast;
  int nofragref; /* hasNoFragmentReference */
  const bim_schema_t *schemas;
  int nschemas;
  int schema; /* current element's */
  int hot; /* SchemaModeStatus */
  int mpegb;
  unsigned compat;
  int depth;
  int level; /* element nesting */
  const bim_decode_opts_t *opts;
  bim_inflate_fn inflate;
  unsigned char *zbuf; /* zlib codec buffer, BufferSize bytes */
  size_t zlen;
  size_t zpos;
  dstrbuf_t vb; /* text of value being decoded, reused */
} dec_t;

/* append, 0 ok, -1 oom (b->buf NULL) */
int dec_add_n(dstrbuf_t *b, const char *s, size_t n);
int dec_add(dstrbuf_t *b, const char *s);
/* value text appended to b */
int dec_text(dec_t *d, const bim_simple_t *s, dstrbuf_t *b);
int dec_plain(dec_t *d, const bim_simple_t *s, dstrbuf_t *b);
int dec_zstring(dec_t *d, dstrbuf_t *b);
void dec_float_render(double x, int prec, char *buf, size_t cap);
int dec_list_count(dec_t *d, const bim_simple_t *s, uint64_t *n);
/* ISO 23001-1 8.2 .. 8.8, m: mapping of s */
int dec_aod(dec_t *d, const bim_simple_t *s, const bim_aod_map_t *m, dstrbuf_t *b);
void dec_init(dec_t *d, const bim_decode_opts_t *opts, bitreader_t *br, strrepo_reader_t *sr);
void dec_done(dec_t *d);
int dec_value(dec_t *d, const bim_simple_t *s, char **out);
int dec_element(dec_t *d, const bim_part_t *p, const bim_type_t *t, bim_node_t *k, int top, int *deferred);
int dec_add_child(dec_t *d, bim_node_t *node, const bim_part_t *p, const bim_type_t *t, const char *name, const char *ns);
/* ISO 23001-1 7.4.8, 7.4.9 */
int dec_fragment_reference(dec_t *d, const bim_type_t *t, bim_node_t *k, int *deferred);
/* 7.5.2.4.5.2 (Amd 2) */
int dec_any_element(dec_t *d, bim_node_t *node);
/* 7.5.3.2, 7.5.3.3 (Amd 2) */
int dec_any_attributes(dec_t *d, bim_node_t *node);
/* Amd 2 7.5.2.4.5.4, bits: rest of AnyElement */
int dec_lax(dec_t *d, bitreader_t *br, size_t bits, bim_node_t *node);

#endif
