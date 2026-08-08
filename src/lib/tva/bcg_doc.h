/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_BCG_DOC_H
#define LIB_BCG_DOC_H

#include <stddef.h>

#define BCG_ID_LEN 256
#define BCG_TEXT_LEN 1024
#define BCG_TIME_LEN 32 /* ISO8601 with offset: YYYY-MM-DDTHH:MM:SS+HH:MM */
#define BCG_MAX_NAMES 8

typedef struct {
  char id[BCG_ID_LEN]; /* raw xmltv id, verbatim */
  char names[BCG_MAX_NAMES][BCG_ID_LEN];
  int name_count;
  char uri[BCG_ID_LEN];
  unsigned tsid, onid, sid; /* best-effort, never a key */
} bcg_channel_t;

typedef struct {
  char channel_id[BCG_ID_LEN];
  char start[BCG_TIME_LEN];
  char stop[BCG_TIME_LEN];
  char title[BCG_TEXT_LEN];
  char desc[BCG_TEXT_LEN];
  char category[BCG_ID_LEN];
} bcg_programme_t;

typedef struct {
  bcg_channel_t *channels;
  int channel_count, channel_cap;
  bcg_programme_t *programmes;
  int programme_count, programme_cap;
} bcg_doc_t;

void bcg_doc_init(bcg_doc_t *d);
void bcg_doc_free(bcg_doc_t *d);

/* grows the backing array as needed, returns a zeroed new entry */
bcg_channel_t *bcg_add_channel(bcg_doc_t *d);
bcg_programme_t *bcg_add_programme(bcg_doc_t *d);

/* NULL if no channel with that id */
const bcg_channel_t *bcg_find_channel(const bcg_doc_t *d, const char *id);

/* appends, silently dropped if already at BCG_MAX_NAMES */
void bcg_channel_add_name(bcg_channel_t *c, const char *name);

#endif
