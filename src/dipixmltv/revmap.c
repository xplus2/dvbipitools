/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "revmap.h"
#include "version.h"

static int split_first1(char *line, char **uri, char **id) {
  char *p1 = strchr(line, ',');
  if (!p1)
    return -1;
  *p1 = '\0';
  *uri = line;
  *id = p1 + 1;
  return 0;
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
    size_t l = strlen(line);
    char *uri, *id;
    revmap_entry_t *e;
    lineno++;
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    if (!line[0] || line[0] == '#')
      continue;
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
    snprintf(e->uri, sizeof e->uri, "%s", uri);
    snprintf(e->id, sizeof e->id, "%s", id);
  }
  fclose(f);
  return 0;
}

void revmap_free(revmap_t *m) {
  free(m->entries);
  memset(m, 0, sizeof *m);
}

const char *revmap_lookup(const revmap_t *m, const char *uri) {
  int i;
  for (i = 0; i < m->count; i++)
    if (!strcmp(m->entries[i].uri, uri))
      return m->entries[i].id;
  return NULL;
}
