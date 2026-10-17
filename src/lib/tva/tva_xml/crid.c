/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "lib/tva/tva_xml.h"
#include "lib/tva/tva_xml/tva_xml_int.h"

#define CONTENTCS_2011 "urn:tva:metadata:cs:ContentCS:2011:"

typedef struct {
  const char *name;
  const char *href;
} genre_map_t;

static const genre_map_t genre_map[] = {
{"news", CONTENTCS_2011 "3.1.1"},
{"nachrichten", CONTENTCS_2011 "3.1.1"},
{"religion", CONTENTCS_2011 "3.1.2"},
{"documentary", CONTENTCS_2011 "3.1.3"},
{"dokumentation", CONTENTCS_2011 "3.1.3"},
{"arts", CONTENTCS_2011 "3.1.4"},
{"culture", CONTENTCS_2011 "3.1.4"},
{"science", CONTENTCS_2011 "3.1.6"},
{"information", CONTENTCS_2011 "3.1"},
{"non-fiction", CONTENTCS_2011 "3.1"},
{"sports", CONTENTCS_2011 "3.2"},
{"sport", CONTENTCS_2011 "3.2"},
{"fiction", CONTENTCS_2011 "3.4"},
{"drama", CONTENTCS_2011 "3.4"},
{"movie", CONTENTCS_2011 "3.4"},
{"film", CONTENTCS_2011 "3.4"},
{"series", CONTENTCS_2011 "3.4"},
{"soap", CONTENTCS_2011 "3.4.2"},
{"romance", CONTENTCS_2011 "3.4.3"},
{"action", CONTENTCS_2011 "3.4.6"},
{"fantasy", CONTENTCS_2011 "3.4.7"},
{"musical", CONTENTCS_2011 "3.4.10"},
{"entertainment", CONTENTCS_2011 "3.5"},
{"quiz", CONTENTCS_2011 "3.5.2"},
{"game show", CONTENTCS_2011 "3.5.2"},
{"variety", CONTENTCS_2011 "3.5.3"},
{"reality", CONTENTCS_2011 "3.5.5"},
{"comedy", CONTENTCS_2011 "3.5.7"},
{"talk", CONTENTCS_2011 "3.5.16"},
{"music", CONTENTCS_2011 "3.6"},
{"classical music", CONTENTCS_2011 "3.6.1"},
{"jazz", CONTENTCS_2011 "3.6.2"},
{"pop", CONTENTCS_2011 "3.6.4"},
{"rock", CONTENTCS_2011 "3.6.4"},
{"games", CONTENTCS_2011 "3.7"},
{"video games", CONTENTCS_2011 "3.7.1"},
{"lifestyle", CONTENTCS_2011 "3.8"},
{"leisure", CONTENTCS_2011 "3.8"},
{"technology", CONTENTCS_2011 "3.8.2"},
{"cooking", CONTENTCS_2011 "3.8.3"},
{"food", CONTENTCS_2011 "3.8.3"},
{"travel", CONTENTCS_2011 "3.8.9"},
{"adult", CONTENTCS_2011 "3.9"},
};

const char *tva_genre_href(const char *category) {
  for (size_t i = 0; i < sizeof genre_map / sizeof genre_map[0]; i++)
    if (!strcasecmp(category, genre_map[i].name)) return genre_map[i].href;
  return CONTENTCS_2011 "3.0";
}

void tva_build_crid(const char *channel_id, const char *start_iso, char *out, size_t outcap) {
  char enc[BCG_ID_LEN * 3];
  char ts[BCG_TIME_LEN];
  pct_encode_seg(channel_id, enc, sizeof enc);
  if (iso8601_utc_compact(start_iso, ts, sizeof ts) != 0) {
    iso8601_compact_prefix(start_iso, ts, sizeof ts);
    if (strlen(ts) >= 14) ts[14] = '\0';
  }
  snprintf(out, outcap, "crid://dipixmltv.invalid/%s/%s", enc, ts);
}

int tva_programme_cache_crid(bcg_programme_t *pr) {
  char crid[TVA_CRID_BUF];
  char *dup;
  tva_build_crid(pr->channel_id, pr->start, crid, sizeof crid);
  dup = strdup(crid);
  if (!dup) return -1;
  free(pr->crid);
  pr->crid = dup;
  return 0;
}

const char *tva_programme_crid(const bcg_programme_t *pr, char *buf, size_t cap) {
  if (pr->crid) return pr->crid;
  tva_build_crid(pr->channel_id, pr->start, buf, cap);
  return buf;
}
