/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_TVHEAD_MPTS_PRIV_H
#define DIPITVHEAD_TVHEAD_MPTS_PRIV_H

#include <poll.h>

#include "lib/demux/tspack.h"
#include "lib/mux/mpts.h"
#include "lib/net/retryset.h"

#include "../../cas/cas.h"
#include "../../input/source.h"
#include "../priv.h"

/* --cas-pids-video/audio: wait this long for every input's live ES discovery,
   then fail fast naming stragglers, vs partial pid list or blocking forever.
   numeric --cas-pids skips this, starts eagerly at t=0, dipiradiohead's scheme */
#define CAS_KEYWORD_DISCOVERY_TIMEOUT_S 15.0

typedef struct {
  const config_t *cfg;
  const dipitvhead_input_t *input;
  input_metrics_t *im;
} tv_slot_ctx_t;

extern const retryset_ops_t tv_retry_ops;

typedef struct {
  unsigned char buf[65536];
  size_t len;
  size_t off; /* off..len: not yet fed to tspack_feed() this read */
} read_backlog_t;

typedef struct {
  /* kept alive past discovery: remux_t's es[] borrows pointers into it */
  psi_t *psi;
  discover_state_t ds;
  double discover_start;

  /* steady state, live once discovered */
  remux_t *rx;
  tspack_t pz;
  read_backlog_t backlog;
} mpts_program_t;

void program_reset(mpts_program_t *p);

typedef struct {
  retryset_t *rs;
  const config_t *cfg;
  mpts_program_t *progs;
  mpts_t *mpts;
  cas_t *cas;
  input_metrics_t *input_stats;
  int metrics_on;
  out_ctx_t *out;
  ts_metrics_t *tsm;
  double now;
  time_t now_t;
} mpts_tick_t;

/* resolves the fd/events to poll for this input: its retry-connect fd, or once connected,
   its live source fd (POLLIN only) */
int poll_fd_for_input(const retryset_t *rs, unsigned i, short *events_out);
int input_poll_ready(unsigned i, const unsigned *pfd_slot, const struct pollfd *pfds, nfds_t npfd);

/* runs (or continues) PSI discovery for input i until a remux_t can be built. always
   settles this visit (never falls through to feed_input() in the same tick) */
void discover_input(mpts_tick_t *tk, unsigned i, tvsrc_t *src);

/* feeds already-buffered/newly-read bytes for input i's live remux_t into tspack_feed() */
void feed_input(mpts_tick_t *tk, unsigned i, tvsrc_t *src);

extern const mpts_program_ops_t mpts_program_ops;

void tvhead_mpts_set_cas(mpts_t *mpts, cas_t *cas);

/* once every input's rx exists (all keyword-CAS pids resolved), starts CAS with the full
   pid list. past the gate deadline without that, fails fast naming the stragglers.
   0: ok (*cas_out set if cas just started). -1: fatal, caller must abort */
int check_cas_discovery_gate(const config_t *cfg, mpts_program_t *progs, unsigned n, mpts_t *mpts,
                             double cas_gate_deadline, cas_t **cas_out);

/* EMM passthrough: mux-wide CAT, merged from each program's descriptor.
   exclusive with own CAS (remux_new()). mp version of remux.c send_psi_tables()'s CAT handler */
void emit_source_cat_passthrough(const mpts_program_t *progs, unsigned n, unsigned char *cat_cc, ts_metrics_t *tsm_p, remux_packet_cb cb, void *ctx);

#endif
