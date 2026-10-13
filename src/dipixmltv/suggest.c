/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/log.h"
#include "lib/helper/playlist_in.h"
#include "lib/sys/ioutil.h"
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
  if (nn == 0) return 0;
  if (nn > hn) return 0;
  for (size_t i = 0; i + nn <= hn; i++) {
    if (!strncasecmp(hay + i, needle, nn)) return 1;
  }
  return 0;
}

static int load_scan(FILE *f, scan_entry_t **out, int *out_n) {
  playlist_list_t *pl;
  scan_entry_t *scan;
  int line = 0;
  int rc = playlist_in_read_csv(f, &pl, &line);

  if (rc == PLAYLIST_CSV_LONG_LINE)
    log_code("XS400207", "scan: line %d: longer than %d bytes", line, PLAYLIST_CSV_LINE_MAX - 1);
  else if (rc == PLAYLIST_CSV_BAD_NUM)
    log_code("XS400209", "scan: line %d: bad tsid/onid/sid", line);
  if (rc)
    return -1;
  scan = calloc(pl->count ? (size_t)pl->count : 1, sizeof *scan);
  if (!scan) {
    playlist_list_free(pl);
    return -1;
  }
  for (int i = 0; i < pl->count; i++) {
    const playlist_item_t *it = &pl->items[i];
    if (strlen(it->name) >= sizeof scan[i].name || strlen(it->uri) >= sizeof scan[i].uri) {
      log_code("XS400208", "scan: entry %d: name or uri longer than %zu bytes", i + 1, sizeof scan[i].name - 1);
      free(scan);
      playlist_list_free(pl);
      return -1;
    }
    bufcpy(scan[i].name, sizeof scan[i].name, it->name);
    bufcpy(scan[i].uri, sizeof scan[i].uri, it->uri);
    scan[i].tsid = it->tsid;
    scan[i].onid = it->onid;
    scan[i].sid = it->sid;
  }
  *out = scan;
  *out_n = pl->count;
  playlist_list_free(pl);
  return 0;
}

/* dipiscan drops commas from names, do the same; dst >= BCG_ID_LEN */
static void strip_commas(char *dst, const char *src) {
  for (; *src; src++)
    if (*src != ',') *dst++ = *src;
  *dst = 0;
}

/* returns scan[] index of an exact case-insensitive name match, or -1 */
static int find_exact_match(const bcg_channel_t *c, const scan_entry_t *scan, const name_index_t *idx) {
  int best = -1;
  for (int k = 0; k < c->name_count; k++) {
    char name[BCG_ID_LEN];
    int j;
    strip_commas(name, c->names[k]);
    if (!name[0]) continue;
    j = name_index_find(idx, scan, name);
    if (j >= 0 && (best < 0 || j < best)) best = j;
  }
  return best;
}

/* returns scan[] index of the substring name match with smallest length difference, or -1; first wins ties */
static int find_fuzzy_match(const bcg_channel_t *c, const scan_entry_t *scan, int scan_n) {
  char names[BCG_MAX_NAMES][BCG_ID_LEN];
  int best = -1;
  size_t best_diff = 0;
  for (int k = 0; k < c->name_count; k++) strip_commas(names[k], c->names[k]);
  for (int j = 0; j < scan_n; j++) {
    size_t sn = strlen(scan[j].name);
    for (int k = 0; k < c->name_count; k++) {
      size_t nn = strlen(names[k]);
      size_t diff = sn > nn ? sn - nn : nn - sn;
      if (best >= 0 && diff >= best_diff) continue;
      if (ci_contains(scan[j].name, names[k]) || ci_contains(names[k], scan[j].name)) {
        best = j;
        best_diff = diff;
      }
    }
  }
  return best;
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
