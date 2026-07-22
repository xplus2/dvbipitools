/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXMLTV_REVMAP_H
#define DIPIXMLTV_REVMAP_H

#include "lib/tva/epg_doc.h"

typedef struct {
  char uri[EPG_ID_LEN];
  char id[EPG_ID_LEN]; /* may contain commas */
} revmap_entry_t;

typedef struct {
  revmap_entry_t *entries;
  int count, cap;
} revmap_t;

/* csv: uri,id */
int revmap_load(const char *path, revmap_t *m);
void revmap_free(revmap_t *m);

/* NULL if not found */
const char *revmap_lookup(const revmap_t *m, const char *uri);

#endif
