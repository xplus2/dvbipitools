/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/demux/tspack.h"
#include "priv.h"

mkv_t *mkv_new(int fd, const mkv_opts_t *opts, int video_ok, unsigned long long *bytes,
               const unsigned *pmt_pids, int n_pids) {
  mkv_t *m = calloc(1, sizeof *m);

  if (!m)
    return NULL;
  m->pend_arena = malloc(MKV_PEND_BYTES);
  if (!m->pend_arena) {
    mkv_close(m);
    return NULL;
  }
  m->fd = fd;
  m->opts = opts;
  m->video_ok = video_ok;
  m->bytes = bytes;
  if (n_pids > MKV_MAX_PROGRAMS)
    n_pids = MKV_MAX_PROGRAMS;
  m->npsi = (n_pids > 0) ? n_pids : 1;
  for (int i = 0; i < m->npsi; i++) {
    m->psi[i] = psi_new();
    if (!m->psi[i]) {
      mkv_close(m);
      return NULL;
    }
    if (n_pids > 0)
      psi_select_pmt_pid(m->psi[i], pmt_pids[i]);
  }
  m->pes = pes_new(on_pes, m);
  if (!m->pes) {
    mkv_close(m);
    return NULL;
  }
  return m;
}

static int all_psi_ready(const mkv_t *m) {
  for (int i = 0; i < m->npsi; i++)
    if (!psi_ready(m->psi[i]))
      return 0;
  return 1;
}

void mkv_feed(mkv_t *m, const unsigned char *pkt) {
  unsigned pid;
  if (m->err)
    return;
  pid = tspack_pid(pkt);
  for (int i = 0; i < m->npsi; i++)
    if (psi_wants_pid(m->psi[i], pid))
      psi_feed(m->psi[i], pkt);
  if (!m->setup && all_psi_ready(m))
    setup(m);
  if (m->setup)
    pes_feed(m->pes, pkt);
  if (m->setup && !m->started && m->ntrk)
    all_ready(m); /* re-check: SDT may arrive later */
}

const psi_t *mkv_psi(const mkv_t *m) { return m->psi[0]; }

int mkv_error(const mkv_t *m) { return m->err; }

void mkv_close(mkv_t *m) {
  if (!m)
    return;
  m->flushing = 1;
  if (m->pes)
    pes_flush(m->pes);
  for (int i = 0; i < m->ntrk; i++)
    if (m->trk[i].ttx)
      ttx_flush(m->trk[i].ttx);
  if (!m->started && m->ntrk)
    start(m);
  cluster_flush(m);
  for (int i = 0; i < m->ntrk; i++) {
    free(m->trk[i].rem);
    free(m->trk[i].vbuf);
    ttx_free(m->trk[i].ttx);
  }
  ebuf_free(&m->cl);
  pes_free(m->pes);
  for (int i = 0; i < m->npsi; i++)
    psi_free(m->psi[i]);
  free(m->pend_arena);
  free(m);
}
