/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mapping.h"

static int split_last4(char *line, char **id, char **uri, char **a, char **b, char **c) {
  char *p4 = strrchr(line, ',');
  char *p3, *p2, *p1;
  if (!p4)
    return -1;
  *p4 = '\0';
  p3 = strrchr(line, ',');
  if (!p3)
    return -1;
  *p3 = '\0';
  p2 = strrchr(line, ',');
  if (!p2)
    return -1;
  *p2 = '\0';
  p1 = strrchr(line, ',');
  if (!p1)
    return -1;
  *p1 = '\0';
  *id = line;
  *uri = p1 + 1;
  *a = p2 + 1;
  *b = p3 + 1;
  *c = p4 + 1;
  return 0;
}

int mapping_load(const char *path, mapping_t *m) {
  FILE *f = fopen(path, "r");
  char line[1024];
  int lineno = 0;

  memset(m, 0, sizeof *m);
  if (!f) {
    fprintf(stderr, "mapping: cannot open %s\n", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    size_t l = strlen(line);
    char *id, *uri, *tsid_s, *onid_s, *sid_s;
    mapping_entry_t *e;
    lineno++;
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    if (!line[0] || line[0] == '#')
      continue;
    if (split_last4(line, &id, &uri, &tsid_s, &onid_s, &sid_s)) {
      fprintf(stderr, "mapping: line %d: expected id,uri,tsid,onid,sid\n", lineno);
      fclose(f);
      mapping_free(m);
      return -1;
    }
    if (m->count >= m->cap) {
      int newcap = m->cap ? m->cap * 2 : 64;
      void *p = realloc(m->entries, (size_t)newcap * sizeof *m->entries);
      if (!p) {
        fclose(f);
        mapping_free(m);
        return -1;
      }
      m->entries = p;
      m->cap = newcap;
    }
    e = &m->entries[m->count++];
    snprintf(e->id, sizeof e->id, "%s", id);
    snprintf(e->uri, sizeof e->uri, "%s", uri);
    e->tsid = (unsigned)strtoul(tsid_s, NULL, 10);
    e->onid = (unsigned)strtoul(onid_s, NULL, 10);
    e->sid = (unsigned)strtoul(sid_s, NULL, 10);
  }
  fclose(f);
  return 0;
}

void mapping_free(mapping_t *m) {
  free(m->entries);
  memset(m, 0, sizeof *m);
}

int mapping_lookup(const mapping_t *m, const char *id, char *uri, size_t uri_cap, unsigned *tsid, unsigned *onid, unsigned *sid) {
  int i;
  for (i = 0; i < m->count; i++)
    if (!strcmp(m->entries[i].id, id)) {
      snprintf(uri, uri_cap, "%s", m->entries[i].uri);
      *tsid = m->entries[i].tsid;
      *onid = m->entries[i].onid;
      *sid = m->entries[i].sid;
      return 0;
    }
  return -1;
}
