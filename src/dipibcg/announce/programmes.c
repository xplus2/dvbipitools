/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipibcg/announce.h"
#include "dipibcg/container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/net/announce_driver.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/net/netconnect.h"
#include "lib/sys/signal.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"
#include "dipibcg/version.h"
#include "dipibcg/wrapper.h"
#include "dipibcg/announce/announce_int.h"

typedef struct {
  const bcg_programme_t *pr;
  long start;
  long end;
  int has_end;
  int idx;
} prog_ord_t;

static int prog_ord_cmp(const void *a, const void *b) {
  const prog_ord_t *x = a;
  const prog_ord_t *y = b;
  int c = strcmp(x->pr->channel_id, y->pr->channel_id);
  if (c) return c;
  return (x->start > y->start) - (x->start < y->start);
}

static void resolve_open_ends(prog_ord_t *ord, int n) {
  for (int i = 0; i < n; i++) {
    int j = i + 1;
    if (ord[i].has_end) continue;
    ord[i].end = ord[i].start;
    while (j < n && !strcmp(ord[j].pr->channel_id, ord[i].pr->channel_id) && ord[j].start == ord[i].start) j++;
    if (j < n && !strcmp(ord[j].pr->channel_id, ord[i].pr->channel_id)) {
      ord[i].end = ord[j].start;
      ord[i].has_end = 1;
    }
  }
}

int prog_times_build(const bcg_doc_t *doc, prog_time_t **out) {
  int n = 0;
  prog_ord_t *ord = doc->programme_count > 0 ? malloc(sizeof *ord * (size_t)doc->programme_count) : NULL;
  prog_time_t *t = doc->programme_count > 0 ? calloc((size_t)doc->programme_count, sizeof *t) : NULL;

  *out = NULL;
  if (doc->programme_count > 0 && (!ord || !t)) {
    free(ord);
    free(t);
    return -1;
  }
  for (int i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    long e;
    if (iso8601_to_minutes(pr->start, &ord[n].start)) continue;
    ord[n].pr = pr;
    ord[n].idx = i;
    ord[n].has_end = pr->stop[0] && !iso8601_to_minutes(pr->stop, &e);
    ord[n].end = ord[n].has_end ? e : 0;
    n++;
  }
  if (n > 1) qsort(ord, (size_t)n, sizeof *ord, prog_ord_cmp);
  resolve_open_ends(ord, n);
  for (int i = 0; i < n; i++) {
    prog_time_t *p = &t[ord[i].idx];
    p->start = ord[i].start;
    p->end = ord[i].end;
    p->has_end = ord[i].has_end;
    p->valid = 1;
  }
  free(ord);
  *out = t;
  return 0;
}

void window_select(const bcg_doc_t *src, const prog_time_t *times, long now, int64_t window_min, int *sel, int *nsel, long *out_sched_start, long *out_sched_end, int *out_have_sched) {
  long sched_start = 0;
  long sched_end = 0;
  int have_sched = 0;
  int n = 0;

  for (int i = 0; i < src->programme_count; i++) {
    const prog_time_t *t = &times[i];
    if (!t->valid) continue;
    if (t->has_end && t->end < now) continue;
    if ((int64_t)t->start > (int64_t)now + window_min) continue;
    sel[n++] = i;
    if (!have_sched || t->start < sched_start) sched_start = t->start;
    if (!have_sched || t->end > sched_end) sched_end = t->end;
    have_sched = 1;
  }
  *nsel = n;
  if (out_sched_start) *out_sched_start = sched_start;
  if (out_sched_end) *out_sched_end = sched_end;
  if (out_have_sched) *out_have_sched = have_sched;
}

int window_view(bcg_doc_t *src, const int *sel, int nsel, bcg_doc_t *dst) {
  memset(dst, 0, sizeof *dst);
  dst->channels = src->channels;
  dst->channel_count = src->channel_count;
  if (nsel <= 0) return 0;
  dst->programmes = malloc(sizeof *dst->programmes * (size_t)nsel);
  if (!dst->programmes) return -1;
  for (int i = 0; i < nsel; i++) dst->programmes[i] = src->programmes[sel[i]];
  dst->programme_count = nsel;
  dst->programme_cap = nsel;
  return 0;
}

void window_view_free(bcg_doc_t *view) {
  free(view->programmes);
  memset(view, 0, sizeof *view);
}
