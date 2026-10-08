/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "mapping.h"

static int split_last4(char *line, char **id, char **uri, char **a, char **b, char **c) {
  char *parts[4];
  for (int i = 0; i < 4; i++) {
    char *comma = strrchr(line, ',');
    if (!comma) return -1;
    *comma = '\0';
    parts[i] = comma + 1;
  }
  *id = line;
  *uri = parts[3];
  *a = parts[2];
  *b = parts[1];
  *c = parts[0];
  return 0;
}

static int mapping_idx_cmp(const void *a, const void *b) {
  return strcmp(((const mapping_idx_t *)a)->id, ((const mapping_idx_t *)b)->id);
}

int mapping_load(const char *path, mapping_t *m) {
  FILE *f = fopen(path, "r");
  char line[1024];
  int lineno = 0;

  memset(m, 0, sizeof *m);
  if (!f) {
    log_line("mapping: cannot open %s", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char *id, *uri, *tsid_s, *onid_s, *sid_s;
    mapping_entry_t *e;
    unsigned tsid;
    unsigned onid;
    unsigned sid;
    lineno++;
    chomp(line);
    if (!line[0] || line[0] == '#')
      continue;
    if (split_last4(line, &id, &uri, &tsid_s, &onid_s, &sid_s)) {
      log_line("mapping: line %d: expected id,uri,tsid,onid,sid", lineno);
      fclose(f);
      mapping_free(m);
      return -1;
    }
    if (argutil_uint_range(tsid_s, 0, 0xFFFF, &tsid) || argutil_uint_range(onid_s, 0, 0xFFFF, &onid) || argutil_uint_range(sid_s, 0, 0xFFFF, &sid)) {
      log_line("mapping: line %d: bad tsid/onid/sid", lineno);
      fclose(f);
      mapping_free(m);
      return -1;
    }
    if (m->count >= m->cap) {
      void *p = array_grow(m->entries, &m->cap, m->count + 1, sizeof *m->entries);
      if (!p) {
        fclose(f);
        mapping_free(m);
        return -1;
      }
      m->entries = p;
    }
    e = &m->entries[m->count++];
    bufcpy(e->id, sizeof e->id, id);
    bufcpy(e->uri, sizeof e->uri, uri);
    e->tsid = tsid;
    e->onid = onid;
    e->sid = sid;
  }
  fclose(f);
  if (m->count > 0) {
    m->idx = malloc(sizeof *m->idx * (size_t)m->count);
    if (m->idx) {
      for (int i = 0; i < m->count; i++) {
        m->idx[i].id = m->entries[i].id;
        m->idx[i].idx = i;
      }
      qsort(m->idx, (size_t)m->count, sizeof *m->idx, mapping_idx_cmp);
    }
  }
  return 0;
}

void mapping_free(mapping_t *m) {
  free(m->entries);
  free(m->idx);
  memset(m, 0, sizeof *m);
}

static int mapping_idx_find(const mapping_t *m, const char *id) {
  int lo = 0;
  int hi = m->count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(id, m->idx[mid].id);
    if (c == 0) return m->idx[mid].idx;
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
  return -1;
}

int mapping_lookup(const mapping_t *m, const char *id, char *uri, size_t uri_cap, unsigned *tsid, unsigned *onid, unsigned *sid) {
  int i = -1;
  if (m->idx) {
    i = mapping_idx_find(m, id);
  } else {
    for (int k = 0; k < m->count; k++) {
      if (!strcmp(m->entries[k].id, id)) {
        i = k;
        break;
      }
    }
  }
  if (i < 0) return -1;
  bufcpy(uri, uri_cap, m->entries[i].uri);
  *tsid = m->entries[i].tsid;
  *onid = m->entries[i].onid;
  *sid = m->entries[i].sid;
  return 0;
}
