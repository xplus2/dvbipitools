/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_EPG_DOC_H
#define LIB_EPG_DOC_H

#include <stddef.h>

#define EPG_ID_LEN 256
#define EPG_TEXT_LEN 1024
#define EPG_TIME_LEN 32 /* ISO8601 with offset: YYYY-MM-DDTHH:MM:SS+HH:MM */
#define EPG_MAX_NAMES 8

typedef struct {
  char id[EPG_ID_LEN]; /* raw xmltv id, verbatim */
  char names[EPG_MAX_NAMES][EPG_ID_LEN];
  int name_count;
  char uri[EPG_ID_LEN];
  unsigned tsid, onid, sid; /* best-effort, never a key */
} epg_channel_t;

typedef struct {
  char channel_id[EPG_ID_LEN];
  char start[EPG_TIME_LEN];
  char stop[EPG_TIME_LEN];
  char title[EPG_TEXT_LEN];
  char desc[EPG_TEXT_LEN];
  char category[EPG_ID_LEN];
} epg_programme_t;

typedef struct {
  epg_channel_t *channels;
  int channel_count, channel_cap;
  epg_programme_t *programmes;
  int programme_count, programme_cap;
} epg_doc_t;

void epg_doc_init(epg_doc_t *d);
void epg_doc_free(epg_doc_t *d);

/* grows the backing array as needed, returns a zeroed new entry */
epg_channel_t *epg_add_channel(epg_doc_t *d);
epg_programme_t *epg_add_programme(epg_doc_t *d);

/* NULL if no channel with that id */
const epg_channel_t *epg_find_channel(const epg_doc_t *d, const char *id);

/* appends, silently dropped if already at EPG_MAX_NAMES */
void epg_channel_add_name(epg_channel_t *c, const char *name);

#endif
