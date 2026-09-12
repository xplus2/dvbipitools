/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/ioutil.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/xmltv.h"
#include "suggest.h"

typedef struct {
  char name[BCG_ID_LEN];
  char uri[BCG_ID_LEN];
  unsigned tsid, onid, sid;
} scan_entry_t;

typedef struct {
  int *slots; /* scan[] index, -1 empty */
  size_t cap; /* power of two */
} name_index_t;

static uint32_t fnv1a_ci(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)tolower((unsigned char)*s)) * 16777619u;
  return h;
}

static void name_index_build(name_index_t *idx, const scan_entry_t *scan, int scan_n) {
  idx->cap = next_pow2((size_t)(scan_n > 0 ? scan_n : 1) * 2);
  idx->slots = malloc(idx->cap * sizeof *idx->slots);
  if (!idx->slots) {
    idx->cap = 0;
    return;
  }
  for (size_t i = 0; i < idx->cap; i++) idx->slots[i] = -1;
  for (int j = 0; j < scan_n; j++) {
    size_t i = fnv1a_ci(scan[j].name) & (idx->cap - 1);
    for (;;) {
      if (idx->slots[i] == -1) {
        idx->slots[i] = j;
        break;
      }
      if (!strcasecmp(scan[idx->slots[i]].name, scan[j].name)) break; /* dup name, keep earlier j */
      i = (i + 1) & (idx->cap - 1);
    }
  }
}

static void name_index_free(name_index_t *idx) { free(idx->slots); }

/* lowest scan[] index whose name case-insensitively equals name, or -1 */
static int name_index_find(const name_index_t *idx, const scan_entry_t *scan, const char *name) {
  size_t i;
  if (!idx->cap) return -1;
  i = fnv1a_ci(name) & (idx->cap - 1);
  for (size_t n = 0; n < idx->cap; n++, i = (i + 1) & (idx->cap - 1)) {
    int j = idx->slots[i];
    if (j == -1) return -1;
    if (!strcasecmp(scan[j].name, name)) return j;
  }
  return -1;
}

static int ci_contains(const char *hay, const char *needle) {
  size_t hn = strlen(hay), nn = strlen(needle);
  if (nn == 0) return 1;
  if (nn > hn) return 0;
  for (size_t i = 0; i + nn <= hn; i++) if (!strncasecmp(hay + i, needle, nn)) return 1;
  return 0;
}

static int load_scan(FILE *f, scan_entry_t **out, int *out_n) {
  char line[1024];
  scan_entry_t *scan = NULL;
  int n = 0, cap = 0;

  while (fgets(line, sizeof line, f)) {
    char *fields[5];
    size_t nf;
    scan_entry_t *e;
    chomp(line);
    if (!line[0]) continue;
    nf = csv_split(line, fields, 5);
    if (nf < 2) continue;
    if (n >= cap) {
      int newcap = cap ? cap * 2 : 64;
      void *np = realloc(scan, (size_t)newcap * sizeof *scan);
      if (!np) {
        free(scan);
        return -1;
      }
      scan = np;
      cap = newcap;
    }
    e = &scan[n++];
    memset(e, 0, sizeof *e);
    bufcpy(e->name, sizeof e->name, fields[0]);
    bufcpy(e->uri, sizeof e->uri, fields[1]);
    e->tsid = nf > 2 ? (unsigned)strtoul(fields[2], NULL, 10) : 0;
    e->onid = nf > 3 ? (unsigned)strtoul(fields[3], NULL, 10) : 0;
    e->sid = nf > 4 ? (unsigned)strtoul(fields[4], NULL, 10) : 0;
  }
  *out = scan;
  *out_n = n;
  return 0;
}

/* returns scan[] index of an exact case-insensitive name match, or -1 */
static int find_exact_match(const bcg_channel_t *c, const scan_entry_t *scan, const name_index_t *idx) {
  int best = -1;
  for (int k = 0; k < c->name_count; k++) {
    int j = name_index_find(idx, scan, c->names[k]);
    if (j >= 0 && (best < 0 || j < best)) best = j;
  }
  return best;
}

/* returns scan[] index of a substring name match, or -1 */
static int find_fuzzy_match(const bcg_channel_t *c, const scan_entry_t *scan, int scan_n) {
  for (int j = 0; j < scan_n; j++)
    for (int k = 0; k < c->name_count; k++)
      if (ci_contains(scan[j].name, c->names[k]) || ci_contains(c->names[k], scan[j].name))
        return j;
  return -1;
}

int suggest_map(FILE *xmltv_f, FILE *scan_f, FILE *out) {
  bcg_doc_t doc;
  scan_entry_t *scan;
  int scan_n;
  name_index_t idx;

  bcg_doc_init(&doc);
  if (xmltv_read(xmltv_f, &doc)) {
    bcg_doc_free(&doc);
    return -1;
  }
  if (load_scan(scan_f, &scan, &scan_n)) {
    bcg_doc_free(&doc);
    return -1;
  }
  name_index_build(&idx, scan, scan_n);

  fputs("# suggested mapping - review before use\n# live lines are exact name matches; commented lines need manual confirmation\n", out);

  for (int i = 0; i < doc.channel_count; i++) {
    bcg_channel_t *c = &doc.channels[i];
    int exact, fuzzy = -1;
    const char *first_name = c->name_count ? c->names[0] : "?";
    exact = find_exact_match(c, scan, &idx);
    if (exact < 0) fuzzy = find_fuzzy_match(c, scan, scan_n);

    if (exact >= 0)
      fprintf(out, "%s,%s,%u,%u,%u\n", c->id, scan[exact].uri, scan[exact].tsid, scan[exact].onid, scan[exact].sid);
    else if (fuzzy >= 0)
      fprintf(out, "# %s (%s) -> closest: %s, %s,%u,%u,%u\n", c->id, first_name, scan[fuzzy].name, scan[fuzzy].uri, scan[fuzzy].tsid, scan[fuzzy].onid, scan[fuzzy].sid);
    else
      fprintf(out, "# UNMATCHED: %s (%s)\n", c->id, first_name);
  }
  name_index_free(&idx);
  free(scan);
  bcg_doc_free(&doc);
  return 0;
}
