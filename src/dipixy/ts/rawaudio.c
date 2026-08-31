/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "rawaudio.h"

#include <stdlib.h>
#include <string.h>

#include "lib/demux/pes.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"

struct rawaudio_demux {
  psi_t *psi;
  pes_t *pes;
  pid_filter_t filter;
  unsigned pid;
  int pid_known;
  int no_audio;
  rawaudio_emit_cb emit;
  void *ctx;
};

#define RAWAUDIO_POOL_MAX 32

static _Thread_local rawaudio_demux_t *t_rawaudio_pool[RAWAUDIO_POOL_MAX];
static _Thread_local int t_rawaudio_pool_n;

static void on_pes(void *vctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len) {
  rawaudio_demux_t *d = vctx;
  (void)pid;
  (void)has_pts;
  (void)pts;
  (void)has_dts;
  (void)dts;
  d->emit(d->ctx, data, len);
}

rawaudio_demux_t *rawaudio_demux_new(unsigned pmt_pid, const pid_filter_t *filter, rawaudio_emit_cb emit, void *ctx) {
  rawaudio_demux_t *d;
  if (t_rawaudio_pool_n > 0)
    d = t_rawaudio_pool[--t_rawaudio_pool_n];
  else
    d = malloc(sizeof *d);
  if (!d)
    return NULL;
  memset(d, 0, sizeof *d);
  d->psi = psi_new();
  if (!d->psi) {
    free(d);
    return NULL;
  }
  if (pmt_pid)
    psi_select_pmt_pid(d->psi, pmt_pid);
  d->pes = pes_new(on_pes, d);
  if (!d->pes) {
    psi_free(d->psi);
    free(d);
    return NULL;
  }
  d->filter = *filter;
  d->emit = emit;
  d->ctx = ctx;
  return d;
}

void rawaudio_demux_free(rawaudio_demux_t *d) {
  if (!d)
    return;
  pes_free(d->pes);
  psi_free(d->psi);
  if (t_rawaudio_pool_n < RAWAUDIO_POOL_MAX)
    t_rawaudio_pool[t_rawaudio_pool_n++] = d;
  else
    free(d);
}

static void rawaudio_pick_pid(rawaudio_demux_t *d) {
  int n, i, best_idx = 0;
  unsigned best_pid = 0;
  const psi_es_t *es = psi_es(d->psi, &n);
  for (i = 0; i < n; i++) {
    if (!es[i].audio_index || pid_filter_excludes(&d->filter, es[i].pid))
      continue;
    if (!best_idx || es[i].audio_index < best_idx) {
      best_idx = es[i].audio_index;
      best_pid = es[i].pid;
    }
  }
  if (best_idx) {
    d->pid = best_pid;
    pes_track(d->pes, d->pid);
    d->pid_known = 1;
  } else {
    d->no_audio = 1;
  }
}

void rawaudio_demux_feed(rawaudio_demux_t *d, const unsigned char *pkt) {
  unsigned pid = tspack_pid(pkt);
  if (d->no_audio)
    return;
  if (!d->pid_known) {
    if (psi_wants_pid(d->psi, pid))
      psi_feed(d->psi, pkt);
    if (psi_ready(d->psi))
      rawaudio_pick_pid(d);
    return;
  }
  if (pid == d->pid)
    pes_feed(d->pes, pkt);
}
