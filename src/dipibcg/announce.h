/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_ANNOUNCE_H
#define DIPIBCG_ANNOUNCE_H

#include <stddef.h>
#include <stdint.h>

#include "lib/metrics/export.h"
#include "lib/tva/bcg_doc.h"

#include "cli/args.h"

int announce_run(const config_t *cfg, metrics_exporter_t *mx);

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]" -> minutes since MJD epoch, UTC-normalized. 0 ok, -1 malformed */
int iso8601_to_minutes(const char *in, long *out);

/* MJD epoch (date_to_mjd 1970-01-01) is day 40587 */
long minutes_to_unix(long mjd_minutes);

typedef struct {
  long start; /* minutes since MJD epoch, UTC */
  long end;   /* valid if has_end, else start */
  int has_end;
  int valid;  /* 0 = unparsable start, never windowed */
} prog_time_t;

/* per-programme times of doc, stopless ends resolved to next start on channel. *out calloc'd, NULL if no programmes. 0 ok, -1 oom */
int prog_times_build(const bcg_doc_t *doc, prog_time_t **out);

void window_select(const bcg_doc_t *src, const prog_time_t *times, long now, int64_t window_min, int *sel, int *nsel, long *out_sched_start, long *out_sched_end, int *out_have_sched);

int window_view(bcg_doc_t *src, const int *sel, int nsel, bcg_doc_t *dst);
void window_view_free(bcg_doc_t *view);

/* reads cfg->input_path (xmltv) and cfg->map_path (csv), applies mapping. 0 ok, -1 error on stderr */
int load_doc(const config_t *cfg, bcg_doc_t *out);

typedef struct {
  unsigned char *last;
  size_t last_len;
  unsigned version;
  int unsent; /* current version not yet sent */
} bcg_version_t;

void version_track(bcg_version_t *v, const unsigned char *cont, size_t cont_len);

#endif
