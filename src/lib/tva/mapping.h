/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_TVA_MAPPING_H
#define DVBIPITOOLS_LIB_TVA_MAPPING_H

#include "bcg_doc.h"

typedef struct {
  char id[BCG_ID_LEN]; /* may contain commas */
  char uri[BCG_ID_LEN]; /* key, not triplet */
  unsigned tsid; /* best-effort, never a key */
  unsigned onid;
  unsigned sid;
} mapping_entry_t;

typedef struct {
  const char *id;
  int idx;
} mapping_idx_t;

typedef struct {
  mapping_entry_t *entries;
  int count;
  int cap;
  mapping_idx_t *idx;
} mapping_t;

/* csv: id,uri,tsid,onid,sid. dipiscan's own csv shape, col 1 = xmltv id */
int mapping_load(const char *path, mapping_t *m);
void mapping_free(mapping_t *m);

/* 0 found, -1 not found */
int mapping_lookup(const mapping_t *m, const char *id, char *uri, size_t uri_cap, unsigned *tsid, unsigned *onid, unsigned *sid);

#endif
