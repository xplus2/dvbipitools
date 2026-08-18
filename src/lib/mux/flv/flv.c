/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/demux/tspack.h"
#include "priv.h"

flv_t *flv_new(const flv_opts_t *opts, unsigned pmt_pid, flv_tag_cb cb, void *cb_ctx, unsigned long long *bytes) {
  flv_t *f = calloc(1, sizeof *f);

  if (!f)
    return NULL;
  f->opts = opts;
  f->bytes = bytes;
  f->cb = cb;
  f->cb_ctx = cb_ctx;
  f->psi = psi_new();
  if (!f->psi) {
    free(f);
    return NULL;
  }
  if (pmt_pid)
    psi_select_pmt_pid(f->psi, pmt_pid);
  f->pes = pes_new(flv_on_pes, f);
  if (!f->pes) {
    psi_free(f->psi);
    free(f);
    return NULL;
  }
  return f;
}

void flv_feed(flv_t *f, const unsigned char *pkt) {
  unsigned pid;

  if (f->err)
    return;
  pid = tspack_pid(pkt);
  if (psi_wants_pid(f->psi, pid))
    psi_feed(f->psi, pkt);
  if (!f->setup && psi_ready(f->psi))
    flv_setup(f);
  if (f->setup)
    pes_feed(f->pes, pkt);
}

const psi_t *flv_psi(const flv_t *f) { return f->psi; }

int flv_error(const flv_t *f) { return f->err; }

void flv_close(flv_t *f) {
  if (!f)
    return;
  f->flushing = 1;
  if (f->pes)
    pes_flush(f->pes);
  free(f->vtrk.rem);
  free(f->vtrk.vbuf);
  free(f->atrk.rem);
  free(f->atrk.vbuf);
  ebuf_free(&f->tagbuf);
  pes_free(f->pes);
  psi_free(f->psi);
  free(f);
}
