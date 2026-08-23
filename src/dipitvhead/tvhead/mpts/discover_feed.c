/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/log.h"
#include "lib/signal.h"
#include "priv.h"

#define MPTS_READ_CHUNK_BYTES (32 * 188) /* caps backlog delay, per input per tick */

void program_reset(mpts_program_t *p) {
  if (p->rx)
    remux_free(p->rx);
  if (p->psi)
    psi_free(p->psi);
  memset(p, 0, sizeof *p);
}

int poll_fd_for_input(const retryset_t *rs, unsigned i, short *events_out) {
  int fd = retryset_poll_fd(rs, i);
  *events_out = retryset_poll_events(rs, i);
  if (fd < 0) {
    const tvsrc_t *src = retryset_result(rs, i);
    if (src) {
      fd = tvsrc_fd(src);
      *events_out = POLLIN;
    }
  }
  return fd;
}

int input_poll_ready(unsigned i, const unsigned *pfd_slot, const struct pollfd *pfds, nfds_t npfd) {
  for (unsigned pfd_i = 0; pfd_i < npfd; pfd_i++)
    if (pfd_slot[pfd_i] == i && (pfds[pfd_i].revents & (POLLIN | POLLERR | POLLHUP)))
      return 1;
  return 0;
}

void discover_input(mpts_tick_t *tk, unsigned i, tvsrc_t *src) {
  int r;

  if (!tk->progs[i].psi) {
    tk->progs[i].psi = psi_new();
    if (!tk->progs[i].psi) {
      log_line("input %u: out of memory allocating psi state, retrying next poll", i);
      return;
    }
    tk->progs[i].discover_start = mono_seconds();
    if (tk->cfg->inputs[i].pmt_pid)
      psi_select_pmt_pid(tk->progs[i].psi, tk->cfg->inputs[i].pmt_pid);
  }
  r = discover_step(&tk->progs[i].ds, src, &tk->cfg->inputs[i], tk->progs[i].psi, tk->metrics_on ? &tk->input_stats[i] : NULL);
  if (r == 0 && mono_seconds() - tk->progs[i].discover_start < DISCOVERY_TIMEOUT_S)
    return;
  if (r <= 0) {
    if (r == 0)
      log_line("input %u: no live PMT found within %.0fs", i, DISCOVERY_TIMEOUT_S);
    program_reset(&tk->progs[i]);
    retryset_mark_down(tk->rs, i, tk->now_t);
    if (tk->metrics_on)
      tk->input_stats[i].up = 0;
    return;
  }

  {
    out_program_pids_t pids;
    out_program_pids(i, &pids);
    log_line("input %u:", i);
    print_discovered(tk->progs[i].psi);
    tk->progs[i].rx = remux_new(tk->cfg, &tk->cfg->inputs[i], tk->progs[i].psi, &pids, 0);
  }
  if (!tk->progs[i].rx) {
    log_line("input %u: remux setup failed", i);
    psi_free(tk->progs[i].psi);
    tk->progs[i].psi = NULL;
    retryset_mark_down(tk->rs, i, tk->now_t);
    if (tk->metrics_on)
      tk->input_stats[i].up = 0;
    return;
  }
  /* cas already running (non-keyword mode, or a reconnect): attach here too,
     else this program's packets never scramble */
  if (tk->cas)
    remux_set_cas(tk->progs[i].rx, tk->cas);
  /* psi outlives rx: es[].src points into it, both freed together in program_reset() */
  mpts_set_program(tk->mpts, i, tk->progs[i].rx);
}

void feed_input(mpts_tick_t *tk, unsigned i, tvsrc_t *src) {
  feed_ctx_t fc;
  read_backlog_t *bl = &tk->progs[i].backlog;
  size_t remaining = bl->len - bl->off;
  size_t chunk;

  if (remaining == 0) {
    net_err_reason_t reason = NET_ERR_OTHER;
    ssize_t rn = tvsrc_read(src, bl->buf, sizeof bl->buf, &reason);
    input_metrics_note_read(tk->metrics_on ? &tk->input_stats[i] : NULL, rn, reason);
    if (rn < 0) {
      mpts_set_program(tk->mpts, i, NULL);
      program_reset(&tk->progs[i]);
      retryset_mark_down(tk->rs, i, tk->now_t);
      if (tk->metrics_on)
        tk->input_stats[i].up = 0;
      return;
    }
    if (rn == 0)
      return;
    bl->len = (size_t)rn;
    bl->off = 0;
    remaining = bl->len;
  }
  chunk = remaining < MPTS_READ_CHUNK_BYTES ? remaining : MPTS_READ_CHUNK_BYTES;
  fc.rx = tk->progs[i].rx;
  fc.out = tk->out;
  fc.now = tk->now;
  fc.tsm = tk->tsm;
  tspack_feed(&tk->progs[i].pz, bl->buf + bl->off, chunk, remux_cb, &fc);
  bl->off += chunk;
  if (bl->off >= bl->len) {
    bl->off = 0;
    bl->len = 0;
  }
}
