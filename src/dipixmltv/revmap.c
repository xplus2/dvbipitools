/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

#include "revmap.h"
#include "version.h"

static int split_first1(char *line, char **uri, char **id) {
  char *p1 = strchr(line, ',');
  if (!p1) return -1;
  *p1 = '\0';
  *uri = line;
  *id = p1 + 1;
  return 0;
}

static int revmap_idx_cmp(const void *a, const void *b) {
  return strcmp(((const revmap_idx_t *)a)->uri, ((const revmap_idx_t *)b)->uri);
}

int revmap_load(const char *path, revmap_t *m) {
  FILE *f = fopen(path, "r");
  char line[1024];
  int lineno = 0;

  memset(m, 0, sizeof *m);
  if (!f) {
    fprintf(stderr, TOOL_NAME ": cannot open reverse map file %s\n", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char *uri, *id;
    revmap_entry_t *e;
    lineno++;
    chomp(line);
    if (!line[0] || line[0] == '#') continue;
    if (split_first1(line, &uri, &id)) {
      fprintf(stderr, TOOL_NAME ": reverse map line %d: expected uri,id\n", lineno);
      fclose(f);
      revmap_free(m);
      return -1;
    }
    if (m->count >= m->cap) {
      int newcap = m->cap ? m->cap * 2 : 64;
      void *p = realloc(m->entries, (size_t)newcap * sizeof *m->entries);
      if (!p) {
        fclose(f);
        revmap_free(m);
        return -1;
      }
      m->entries = p;
      m->cap = newcap;
    }
    e = &m->entries[m->count++];
    bufcpy(e->uri, sizeof e->uri, uri);
    bufcpy(e->id, sizeof e->id, id);
  }
  fclose(f);
  if (m->count > 0) {
    m->idx = malloc(sizeof *m->idx * (size_t)m->count);
    if (m->idx) {
      for (int i = 0; i < m->count; i++) {
        m->idx[i].uri = m->entries[i].uri;
        m->idx[i].idx = i;
      }
      qsort(m->idx, (size_t)m->count, sizeof *m->idx, revmap_idx_cmp);
    }
  }
  return 0;
}

void revmap_free(revmap_t *m) {
  free(m->entries);
  free(m->idx);
  memset(m, 0, sizeof *m);
}

const char *revmap_lookup(const revmap_t *m, const char *uri) {
  if (m->idx) {
    int lo = 0;
    int hi = m->count - 1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      int c = strcmp(uri, m->idx[mid].uri);
      if (c == 0) return m->entries[m->idx[mid].idx].id;
      if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return NULL;
  }
  for (int i = 0; i < m->count; i++) if (!strcmp(m->entries[i].uri, uri)) return m->entries[i].id;
  return NULL;
}
