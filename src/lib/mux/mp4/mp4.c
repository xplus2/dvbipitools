/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/demux/tspack.h"
#include "priv.h"

mp4_t *mp4_new(int fd, const mp4_opts_t *opts, int video_ok, unsigned long long *bytes, const unsigned *pmt_pids, int n_pids) {
  mp4_t *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->pend_arena = malloc(MP4_PEND_BYTES);
  if (!m->pend_arena) {
    mp4_close(m);
    return NULL;
  }
  m->fd = fd;
  m->opts = opts;
  m->video_ok = video_ok;
  m->bytes = bytes;
  if (n_pids > MP4_MAX_PROGRAMS)
    n_pids = MP4_MAX_PROGRAMS;
  m->npsi = (n_pids > 0) ? n_pids : 1;
  for (int i = 0; i < m->npsi; i++) {
    m->psi[i] = psi_new();
    if (!m->psi[i]) {
      mp4_close(m);
      return NULL;
    }
    if (n_pids > 0) psi_select_pmt_pid(m->psi[i], pmt_pids[i]);
  }
  m->pes = pes_new(p4_on_pes, m);
  if (!m->pes) {
    mp4_close(m);
    return NULL;
  }
  return m;
}

static int all_psi_ready(const mp4_t *m) {
  for (int i = 0; i < m->npsi; i++) if (!psi_ready(m->psi[i])) return 0;
  return 1;
}

void mp4_feed(mp4_t *m, const unsigned char *pkt) {
  unsigned pid;
  if (m->err) return;
  pid = tspack_pid(pkt);
  for (int i = 0; i < m->npsi; i++) if (psi_wants_pid(m->psi[i], pid)) psi_feed(m->psi[i], pkt);
  if (!m->setup && all_psi_ready(m)) p4_setup(m);
  if (m->setup) pes_feed(m->pes, pkt);
  if (m->setup && !m->started && m->ntrk) p4_all_ready(m); /* re-check: SDT may be late */
}

const psi_t *mp4_psi(const mp4_t *m) { return m->psi[0]; }

int mp4_error(const mp4_t *m) { return m->err; }

void mp4_close(mp4_t *m) {
  if (!m) return;
  m->flushing = 1;
  if (m->pes) pes_flush(m->pes);
  for (int i = 0; i < m->ntrk; i++) if (m->trk[i].ttx) ttx_flush(m->trk[i].ttx);
  if (!m->started && m->ntrk) p4_start(m);
  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    if (t->cls == PID_VIDEO && t->prev_dur_slot) *t->prev_dur_slot = t->last_dur ? t->last_dur : 1;
  }
  if (m->started) p4_write_moov(m);
  for (int i = 0; i < m->ntrk; i++) {
    free(m->trk[i].rem);
    free(m->trk[i].vbuf);
    free(m->trk[i].samp);
    ttx_free(m->trk[i].ttx);
  }
  pes_free(m->pes);
  for (int i = 0; i < m->npsi; i++) psi_free(m->psi[i]);
  free(m->pend_arena);
  free(m);
}
