/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "mapping.h"

/* unquotes in place a field starting with '"', "" is a literal quote. returns pointer past closing quote, NULL if unterminated */
static char *unquote_field(char *s) {
  char *w = s;
  char *p = s + 1;
  for (;;) {
    if (!*p) return NULL;
    if (*p == '"') {
      if (p[1] != '"') break;
      p++;
    }
    *w++ = *p++;
  }
  *w = '\0';
  return p + 1;
}

/* last three fields numeric. id,uri quoted when either holds a comma or quote, else id may hold commas */
static int split_last4(char *line, char **id, char **uri, char **a, char **b, char **c) {
  char *parts[3];
  for (int i = 0; i < 3; i++) {
    char *comma = strrchr(line, ',');
    if (!comma) return -1;
    *comma = '\0';
    parts[i] = comma + 1;
  }
  if (line[0] == '"') {
    char *rest = unquote_field(line);
    if (!rest || rest[0] != ',') return -1;
    *uri = rest + 1;
    if ((*uri)[0] == '"') {
      char *end = unquote_field(*uri);
      if (!end || *end) return -1;
    }
  } else {
    char *comma = strrchr(line, ',');
    if (!comma) return -1;
    *comma = '\0';
    *uri = comma + 1;
  }
  *id = line;
  *a = parts[2];
  *b = parts[1];
  *c = parts[0];
  return 0;
}

static int mapping_idx_cmp(const void *a, const void *b) {
  const mapping_idx_t *x = a;
  const mapping_idx_t *y = b;
  int c = strcmp(x->id, y->id);
  if (c) return c;
  return (x->idx > y->idx) - (x->idx < y->idx);
}

int mapping_load(const char *path, mapping_t *m) {
  FILE *f = fopen(path, "r");
  char line[1024];
  int lineno = 0, rc;

  memset(m, 0, sizeof *m);
  if (!f) {
    log_code("LE000326", "mapping: cannot open %s", path);
    return -1;
  }
  while ((rc = read_line(f, line, sizeof line)) != 0) {
    char *id, *uri, *tsid_s, *onid_s, *sid_s;
    mapping_entry_t *e;
    unsigned tsid;
    unsigned onid;
    unsigned sid;
    lineno++;
    if (rc < 0) {
      log_code("LE400205", "mapping: line %d: longer than %zu bytes", lineno, sizeof line - 1);
      fclose(f);
      mapping_free(m);
      return -1;
    }
    chomp(line);
    if (!line[0] || line[0] == '#')
      continue;
    if (split_last4(line, &id, &uri, &tsid_s, &onid_s, &sid_s)) {
      log_code("LE000327", "mapping: line %d: expected id,uri,tsid,onid,sid", lineno);
      fclose(f);
      mapping_free(m);
      return -1;
    }
    if (argutil_uint_range(tsid_s, 0, 0xFFFF, &tsid) || argutil_uint_range(onid_s, 0, 0xFFFF, &onid) || argutil_uint_range(sid_s, 0, 0xFFFF, &sid)) {
      log_code("LE400185", "mapping: line %d: bad tsid/onid/sid", lineno);
      fclose(f);
      mapping_free(m);
      return -1;
    }
    if (strlen(id) >= sizeof e->id || strlen(uri) >= sizeof e->uri) {
      log_code("LE400206", "mapping: line %d: id or uri longer than %zu bytes", lineno, sizeof e->id - 1);
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
      for (int i = 1; i < m->count; i++) {
        if (!strcmp(m->idx[i].id, m->idx[i - 1].id))
          log_code("LW700163", "mapping: duplicate id %s, first entry wins", m->idx[i].id);
      }
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
  int hi = m->count;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (strcmp(m->idx[mid].id, id) < 0) lo = mid + 1; else hi = mid;
  }
  if (lo < m->count && !strcmp(m->idx[lo].id, id)) return m->idx[lo].idx;
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
