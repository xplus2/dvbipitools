/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/ioutil.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/xmltv.h"
#include "suggest.h"

typedef struct {
  char name[BCG_ID_LEN];
  char uri[BCG_ID_LEN];
  unsigned tsid, onid, sid;
} scan_entry_t;

static int ci_contains(const char *hay, const char *needle) {
  size_t hn = strlen(hay), nn = strlen(needle);
  size_t i;
  if (nn == 0)
    return 1;
  if (nn > hn)
    return 0;
  for (i = 0; i + nn <= hn; i++)
    if (!strncasecmp(hay + i, needle, nn))
      return 1;
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
    if (!line[0])
      continue;
    nf = csv_split(line, fields, 5);
    if (nf < 2)
      continue;
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
static int find_exact_match(const bcg_channel_t *c, const scan_entry_t *scan, int scan_n) {
  int j, k;
  for (j = 0; j < scan_n; j++)
    for (k = 0; k < c->name_count; k++)
      if (!strcasecmp(c->names[k], scan[j].name))
        return j;
  return -1;
}

/* returns scan[] index of a substring name match, or -1 */
static int find_fuzzy_match(const bcg_channel_t *c, const scan_entry_t *scan, int scan_n) {
  int j, k;
  for (j = 0; j < scan_n; j++)
    for (k = 0; k < c->name_count; k++)
      if (ci_contains(scan[j].name, c->names[k]) || ci_contains(c->names[k], scan[j].name))
        return j;
  return -1;
}

int suggest_map(FILE *xmltv_f, FILE *scan_f, FILE *out) {
  bcg_doc_t doc;
  scan_entry_t *scan;
  int scan_n, i;

  bcg_doc_init(&doc);
  if (xmltv_read(xmltv_f, &doc)) {
    bcg_doc_free(&doc);
    return -1;
  }
  if (load_scan(scan_f, &scan, &scan_n)) {
    bcg_doc_free(&doc);
    return -1;
  }

  fputs("# suggested mapping - review before use\n# live lines are exact name matches; commented lines need manual confirmation\n", out);

  for (i = 0; i < doc.channel_count; i++) {
    bcg_channel_t *c = &doc.channels[i];
    int exact, fuzzy = -1;
    const char *first_name = c->name_count ? c->names[0] : "?";
    exact = find_exact_match(c, scan, scan_n);
    if (exact < 0)
      fuzzy = find_fuzzy_match(c, scan, scan_n);

    if (exact >= 0)
      fprintf(out, "%s,%s,%u,%u,%u\n", c->id, scan[exact].uri, scan[exact].tsid, scan[exact].onid, scan[exact].sid);
    else if (fuzzy >= 0)
      fprintf(out, "# %s (%s) -> closest: %s, %s,%u,%u,%u\n", c->id, first_name, scan[fuzzy].name, scan[fuzzy].uri, scan[fuzzy].tsid, scan[fuzzy].onid, scan[fuzzy].sid);
    else
      fprintf(out, "# UNMATCHED: %s (%s)\n", c->id, first_name);
  }
  free(scan);
  bcg_doc_free(&doc);
  return 0;
}
