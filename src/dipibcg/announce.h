/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_ANNOUNCE_H
#define DIPIBCG_ANNOUNCE_H

#include "lib/metrics/export.h"
#include "lib/tva/bcg_doc.h"

#include "args.h"

int announce_run(const config_t *cfg, metrics_exporter_t *mx);

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]" -> minutes since MJD epoch, UTC-normalized. 0 ok, -1 malformed */
int iso8601_to_minutes(const char *in, long *out);

/* MJD epoch (date_to_mjd 1970-01-01) is day 40587 */
long minutes_to_unix(long mjd_minutes);

/* dst := channels of src, programmes of src within [now, now+window_min]. 0 ok, -1 oom */
int build_windowed_doc(const bcg_doc_t *src, bcg_doc_t *dst, long now, long window_min);

/* reads cfg->input_path (xmltv) and cfg->map_path (csv), applies mapping. 0 ok, -1 error on stderr */
int load_doc(const config_t *cfg, bcg_doc_t *out);

#endif
