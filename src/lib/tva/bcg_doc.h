/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_TVA_BCG_DOC_H
#define DVBIPITOOLS_LIB_TVA_BCG_DOC_H

#include <stddef.h>
#include <string.h>

#include "../helper/log.h"
#include "../helper/xml_util.h"

#define BCG_ID_LEN 256
#define BCG_TIME_LEN 32 /* ISO8601 with offset: YYYY-MM-DDTHH:MM:SS+HH:MM */
#define BCG_MAX_NAMES 8

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
} bcg_programme_t;

/* CRID-keyed prg text, pending merge into bcg_programme_t. accessunit.c and tva_xml.c
   build their own side-table, different lookup strategies, same rec */
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
} bcg_doc_t;

void bcg_doc_init(bcg_doc_t *d);
void bcg_doc_free(bcg_doc_t *d);

bcg_channel_t *bcg_add_channel(bcg_doc_t *d);
bcg_programme_t *bcg_add_programme(bcg_doc_t *d);

/* NULL-safe text read, absent -> "" */
static inline const char *bcg_str(const char *s) { return s ? s : ""; }

/* *dst = heap copy of s, old value freed. NULL or "" -> NULL. 0 ok, -1 OOM (*dst unchanged) */
int bcg_set_text(char **dst, const char *s);

/* frees title/desc/category, leaves rest */
void bcg_programme_free_text(bcg_programme_t *pr);

/* deep copy, dst overwritten (not freed). 0 ok, -1 OOM (dst holds no text) */
int bcg_programme_copy(bcg_programme_t *dst, const bcg_programme_t *src);

/* *out = text of first <tag> in [s,end), NULL if absent or empty. old value freed. 0 ok, -1 OOM */
int bcg_elem_text(const char *s, const char *end, const char *tag, char **out);

void bcg_progtext_free(bcg_progtext_t *pt);

/* NULL if no channel with that id */
const bcg_channel_t *bcg_find_channel(const bcg_doc_t *d, const char *id);

/* appends, silently dropped if already at BCG_MAX_NAMES */
void bcg_channel_add_name(bcg_channel_t *c, const char *name);

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
