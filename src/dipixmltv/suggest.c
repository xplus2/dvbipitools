/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/tva/epg_doc.h"
#include "suggest.h"
#include "lib/tva/xmltv.h"

typedef struct {
  char name[EPG_ID_LEN];
  char uri[EPG_ID_LEN];
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
    int nf = 0;
    char *p = line;
    size_t l = strlen(line);
    scan_entry_t *e;
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    if (!line[0])
      continue;
    while (nf < 5) {
      fields[nf++] = p;
      p = strchr(p, ',');
      if (!p)
        break;
      *p = '\0';
      p++;
    }
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
    snprintf(e->name, sizeof e->name, "%s", fields[0]);
    snprintf(e->uri, sizeof e->uri, "%s", fields[1]);
    e->tsid = nf > 2 ? (unsigned)strtoul(fields[2], NULL, 10) : 0;
    e->onid = nf > 3 ? (unsigned)strtoul(fields[3], NULL, 10) : 0;
    e->sid = nf > 4 ? (unsigned)strtoul(fields[4], NULL, 10) : 0;
  }
  *out = scan;
  *out_n = n;
  return 0;
}

int suggest_map(FILE *xmltv_f, FILE *scan_f, FILE *out) {
  epg_doc_t doc;
  scan_entry_t *scan;
  int scan_n, i, j, k;

  epg_doc_init(&doc);
  if (xmltv_read(xmltv_f, &doc)) {
    epg_doc_free(&doc);
    return -1;
  }
  if (load_scan(scan_f, &scan, &scan_n)) {
    epg_doc_free(&doc);
    return -1;
  }

  fputs("# suggested mapping - review before use\n# live lines are exact name matches; commented lines need manual confirmation\n", out);

  for (i = 0; i < doc.channel_count; i++) {
    epg_channel_t *c = &doc.channels[i];
    int exact = -1, fuzzy = -1;
    const char *first_name = c->name_count ? c->names[0] : "?";
    for (j = 0; j < scan_n && exact < 0; j++)
      for (k = 0; k < c->name_count; k++)
        if (!strcasecmp(c->names[k], scan[j].name)) {
          exact = j;
          break;
        }
    if (exact < 0)
      for (j = 0; j < scan_n && fuzzy < 0; j++)
        for (k = 0; k < c->name_count; k++)
          if (ci_contains(scan[j].name, c->names[k]) || ci_contains(c->names[k], scan[j].name)) {
            fuzzy = j;
            break;
          }

    if (exact >= 0)
      fprintf(out, "%s,%s,%u,%u,%u\n", c->id, scan[exact].uri, scan[exact].tsid, scan[exact].onid, scan[exact].sid);
    else if (fuzzy >= 0)
      fprintf(out, "# %s (%s) -> closest: %s, %s,%u,%u,%u\n", c->id, first_name, scan[fuzzy].name, scan[fuzzy].uri, scan[fuzzy].tsid, scan[fuzzy].onid, scan[fuzzy].sid);
    else
      fprintf(out, "# UNMATCHED: %s (%s)\n", c->id, first_name);
  }
  free(scan);
  epg_doc_free(&doc);
  return 0;
}
