/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_TVA_BCG_DOC_H
#define DVBIPITOOLS_LIB_TVA_BCG_DOC_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../helper/log.h"
#include "../helper/xml_util.h"

#define BCG_ID_LEN 256
#define BCG_TIME_LEN 32 /* ISO8601 with offset: YYYY-MM-DDTHH:MM:SS+HH:MM */
#define BCG_MAX_NAMES 8
#define BCG_NS_LEN 64
#define BCG_LANG_LEN 36 /* xml:lang, BCP 47 */
#define TVA_NS_DEFAULT "urn:tva:metadata:2004"

typedef struct {
  char id[BCG_ID_LEN]; /* raw xmltv id, verbatim */
  char names[BCG_MAX_NAMES][BCG_ID_LEN];
  int name_count;
  char uri[BCG_ID_LEN];
  unsigned tsid; /* best-effort, never a key */
  unsigned onid;
  unsigned sid;
} bcg_channel_t;

typedef struct {
  char channel_id[BCG_ID_LEN];
  char start[BCG_TIME_LEN];
  char stop[BCG_TIME_LEN];
  char *title; /* heap, NULL = absent, never "" */
  char *desc;
  char *category;
  char *crid; /* heap, optional cache of tva_build_crid, NULL = build on demand */
} bcg_programme_t;

/* CRID-keyed prg text, pending merge into bcg_programme_t */
typedef struct {
  char *crid;
  char *title;
  char *desc;
  char *category;
} bcg_progtext_t;

typedef struct {
  bcg_channel_t *channels;
  int channel_count;
  int channel_cap;
  bcg_programme_t *programmes;
  int programme_count;
  int programme_cap;
  char ns[BCG_NS_LEN]; /* TVA metadata namespace, "" = TVA_NS_DEFAULT */
  char publisher[BCG_ID_LEN]; /* TVAMain, TS 102 323 8.9 "<domain>/<readable name>", "" = absent */
  char lang[BCG_LANG_LEN]; /* TVAMain xml:lang, "" = absent */
} bcg_doc_t;

void bcg_doc_init(bcg_doc_t *d);
void bcg_doc_free(bcg_doc_t *d);

typedef struct {
  const char *key;
  int idx;
} bcg_key_idx_t;

static inline int bcg_key_idx_cmp(const void *a, const void *b) {
  const bcg_key_idx_t *x = a;
  const bcg_key_idx_t *y = b;
  int c = strcmp(x->key, y->key);
  if (c) return c;
  return (x->idx > y->idx) - (x->idx < y->idx);
}

static inline void bcg_key_index_sort(bcg_key_idx_t *ix, int n) {
  if (n > 0) qsort(ix, (size_t)n, sizeof *ix, bcg_key_idx_cmp);
}

static inline int bcg_key_index_find(const bcg_key_idx_t *ix, int n, const char *key) {
  int lo = 0;
  int hi = n;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (strcmp(ix[mid].key, key) < 0) lo = mid + 1;
    else hi = mid;
  }
  return lo < n && !strcmp(ix[lo].key, key) ? ix[lo].idx : -1;
}

/* programme-to-channel map of one doc, valid while doc is unchanged. zero-init not needed, bcg_doc_index_free() when done */
typedef struct {
  bcg_key_idx_t *chan;
  int *nprog; /* per channel */
  int *first; /* per channel, -1 none */
  int *next;  /* per programme, next of same channel, -1 ends */
} bcg_doc_index_t;

/* 0 ok, -1 oom */
int bcg_doc_index_build(bcg_doc_index_t *ix, const bcg_doc_t *doc);
void bcg_doc_index_free(bcg_doc_index_t *ix);
int bcg_progtext_apply(bcg_programme_t *pr, const bcg_progtext_t *pt);
void bcg_channel_dtt_format(const bcg_channel_t *c, char *out, size_t cap);
int bcg_channel_dtt_parse(bcg_channel_t *c, const char *s);

bcg_channel_t *bcg_add_channel(bcg_doc_t *d);
bcg_programme_t *bcg_add_programme(bcg_doc_t *d);

static inline const char *bcg_doc_ns(const bcg_doc_t *d) { return d->ns[0] ? d->ns : TVA_NS_DEFAULT; }

/* NULL-safe text read, absent -> "" */
static inline const char *bcg_str(const char *s) { return s ? s : ""; }

/* *dst = heap copy of s, old value freed. NULL or "" -> NULL. 0 ok, -1 OOM (*dst unchanged) */
int bcg_set_text(char **dst, const char *s);

/* frees title/desc/category/crid, leaves rest */
void bcg_programme_free_text(bcg_programme_t *pr);

/* deep copy, dst overwritten (not freed). 0 ok, -1 OOM (dst holds no text) */
int bcg_programme_copy(bcg_programme_t *dst, const bcg_programme_t *src);

/* *out = text of first <tag> in [s,end), NULL if absent or empty. old value freed. 0 ok, -1 OOM */
int bcg_elem_text(const char *s, const char *end, const char *tag, char **out);

void bcg_progtext_free(bcg_progtext_t *pt);

/* TS 102 323 8.9 "<domain>/<readable name>", name RFC 3986 encoded. 1 valid, 0 not */
int bcg_publisher_valid(const char *pub);
/* %XX decoded readable name of pub. 0 ok, -1 invalid or cap */
int bcg_publisher_name(const char *pub, char *out, size_t cap);
/* xs:language [a-zA-Z]{1,8}(-[a-zA-Z0-9]{1,8})*, below BCG_LANG_LEN. 1 valid, 0 not */
int bcg_lang_valid(const char *lang);

/* NULL if no channel with that id */
const bcg_channel_t *bcg_find_channel(const bcg_doc_t *d, const char *id);

/* appends. -1 if dropped, already at BCG_MAX_NAMES */
int bcg_channel_add_name(bcg_channel_t *c, const char *name);

static inline int bcg_attr(const char *s, const char *end, const char *name, char *out, size_t outcap) {
  int cut = 0;
  if (xml_attr_chk(s, end, name, out, outcap, &cut)) return -1;
  if (cut) log_code("LW400184", "bcg: attribute %s truncated to %zu bytes", name, strlen(out));
  return 0;
}

static inline int bcg_tag_attr(const char *s, const char *end, const char *name, char *out, size_t outcap) {
  int cut = 0;
  if (xml_tag_attr_chk(s, end, name, out, outcap, &cut)) return -1;
  if (cut) log_code("LW400184", "bcg: attribute %s truncated to %zu bytes", name, strlen(out));
  return 0;
}

#endif
