/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"

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
  const revmap_idx_t *x = a;
  const revmap_idx_t *y = b;
  int c = strcmp(x->uri, y->uri);
  if (c) return c;
  return (x->idx > y->idx) - (x->idx < y->idx);
}

int revmap_load(const char *path, revmap_t *m) {
  FILE *f = fopen(path, "r");
  char line[1024];
  int lineno = 0;
  int rc;

  memset(m, 0, sizeof *m);
  if (!f) {
    log_code("XS000261", TOOL_NAME ": cannot open reverse map file %s", path);
    return -1;
  }
  while ((rc = read_line(f, line, sizeof line)) != 0) {
    char *uri, *id;
    revmap_entry_t *e;
    lineno++;
    if (rc < 0) {
      log_code("XS400205", TOOL_NAME ": reverse map line %d: longer than %zu bytes", lineno, sizeof line - 1);
      fclose(f);
      revmap_free(m);
      return -1;
    }
    chomp(line);
    if (!line[0] || line[0] == '#') continue;
    if (split_first1(line, &uri, &id)) {
      log_code("XS400124", TOOL_NAME ": reverse map line %d: expected uri,id", lineno);
      fclose(f);
      revmap_free(m);
      return -1;
    }
    if (strlen(uri) >= sizeof e->uri || strlen(id) >= sizeof e->id) {
      log_code("XS400206", TOOL_NAME ": reverse map line %d: uri or id longer than %zu bytes", lineno, sizeof e->id - 1);
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
      for (int i = 1; i < m->count; i++) {
        if (!strcmp(m->idx[i].uri, m->idx[i - 1].uri))
          log_code("XW400223", TOOL_NAME ": reverse map: duplicate uri %s, first entry wins", m->idx[i].uri);
      }
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
    int hi = m->count;
    while (lo < hi) {
      int mid = lo + (hi - lo) / 2;
      if (strcmp(m->idx[mid].uri, uri) < 0) lo = mid + 1; else hi = mid;
    }
    if (lo < m->count && !strcmp(m->idx[lo].uri, uri)) return m->entries[m->idx[lo].idx].id;
    return NULL;
  }
  for (int i = 0; i < m->count; i++) {
    if (!strcmp(m->entries[i].uri, uri)) return m->entries[i].id;
  }
  return NULL;
}
