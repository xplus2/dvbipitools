/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_CODEC_ENCODE_PRIV_H
#define DVBIPITOOLS_LIB_BIM_CODEC_ENCODE_PRIV_H

#include "lib/bim/codec.h"
#include "lib/bim/codec/codec_int.h"

typedef struct {
  const bim_encode_opts_t *o;
  bitwriter_t *bw;
  strrepo_writer_t *sw;
  dvb_codec_state_t st;
  const bim_schema_t *schemas;
  int nschemas;
  int schema; /* current element's */
  int hot; /* SchemaModeStatus */
  int resync; /* string_offset due before next string */
  int dry; /* match only, no output */
  int depth;
  size_t zsize; /* Zlib BufferSize */
  int zmode; /* 0 chunk per string, 1 collecting strings, 2 emitting planned chunks */
  struct enc_zplan *zp;
  size_t zi; /* strings emitted in mode 2 */
} enc_t;

int enc_bit(bitwriter_t *bw, int v);
const char *enc_skip_ws(const char *s);
/* mapped advanced optimised decoder, else default codec */
int enc_text(enc_t *e, const bim_simple_t *s, const char *v);
int enc_plain(enc_t *e, const bim_simple_t *s, const char *v);
int enc_parse_double(const char *v, double *out);
/* BIM_ZSTRING codec, gathered or per string chunks */
int enc_zstring(enc_t *e, const char *v);
/* strings coded so far when gathering, else 0 */
size_t enc_zprogress(const enc_t *e);
/* one chunk run per string, advanced decoder of ISO 23001-1 8.7 */
int enc_zchunks(enc_t *e, const char *v);
/* payload element with strings gathered up to BufferSize per chunk (two passes) */
int enc_gathered(enc_t *e, const bim_type_t *t, const bim_node_t *root);
/* ISO 23001-1 8.2 .. 8.8, m: mapping of s */
int enc_aod(enc_t *e, const bim_simple_t *s, const bim_aod_map_t *m, const char *v);
/* body into a temporary writer, then length (bits) and body. fn writes to e->bw */
int enc_counted(enc_t *e, int (*fn)(enc_t *e, void *arg), void *arg);
int enc_element(enc_t *e, const bim_part_t *p, const bim_type_t *t, const bim_node_t *k, int top);
/* ISO 23001-1 7.4.8, 7.4.9 */
int enc_fragment_reference(enc_t *e, const bim_node_t *k);
/* 7.5.2.4.5.2 (Amd 2) */
int enc_any_element(enc_t *e, const bim_node_t *k);
/* 7.5.3.2, 7.5.3.3 (Amd 2): attributes of node not declared by t */
int enc_any_attributes(enc_t *e, const bim_type_t *t, const bim_node_t *node);
/* Amd 2 7.5.2.4.5.4: k as lax events after AnyElement_Length */
int enc_lax(enc_t *e, const bim_node_t *k);
/* global element of a known schema other than lax, by name or pointer. sid, code may be NULL */
int enc_global_name(const enc_t *e, const bim_node_t *k, const bim_elem_t **x, int *sid, uint64_t *code);
int enc_global_ptr(const enc_t *e, const bim_elem_t *x, int *sid, uint64_t *code);
int enc_is_nil(const bim_node_t *k);
int enc_wildcard_allows(const bim_wildcard_t *w, const char *ns);
/* attribute not declared by t and not xsi */
int enc_extra_attr(const bim_type_t *t, const bim_node_t *node, int i);

#endif
