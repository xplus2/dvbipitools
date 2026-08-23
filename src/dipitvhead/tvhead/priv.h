/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_TVHEAD_PRIV_H
#define DIPITVHEAD_TVHEAD_PRIV_H

#include <stdint.h>

#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/metrics/export.h"
#include "lib/mux/rtpheader.h"
#include "lib/net/multicast.h"
#include "lib/net/rist/ristout.h"

#include "../cas/cas.h"
#include "../input/source.h"
#include "../mux/bitrate.h"
#include "../mux/remux.h"
#include "tvhead.h"

/* how long to watch PAT-listed PMT candidates before giving up */
#define DISCOVERY_TIMEOUT_S 8.0
#define TS_PER_DGRAM 7

typedef struct {
  tspack_t pz;
  int listed, checked_pmt_pid;
} discover_state_t;

typedef struct {
  mcast_t *mc; /* NULL unless -m given */
  int rtp;
  rtpheader_t *rtph;
  ristout_t *rist; /* NULL unless -R given; bonded peers, sent alongside mc if both present */
  bitrate_pacer_t *pacer;
  unsigned char batch[12 + TS_PER_DGRAM * 188]; /* [0,12): RTP header headroom, unused if !rtp */
  int batch_count;
  int mc_had_error;   /* edge-log gate; a send failure here never stops process */
  int rist_had_error; /* edge-log gate; a write failure here never stops process */
  unsigned long long packets;
  unsigned long long errors;
} out_ctx_t;

typedef struct {
  remux_t *rx;
  out_ctx_t *out;
  double now;
  ts_metrics_t *tsm;
} feed_ctx_t;

/* discover.c */
void print_discovered(const psi_t *psi);
int discover_step(discover_state_t *ds, tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im);
int discover(tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im);

/* output.c */
/* caller only calls this when cfg->n_rist > 0; NULL on err */
ristout_t *tvhead_rist_open(const config_t *cfg);
void flush_batch(out_ctx_t *o);
void packet_cb(void *ctx, const unsigned char *pkt188);
void send_null_packet(out_ctx_t *o);
int remux_cb(void *v, const unsigned char *pkt);
void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
                   const input_metrics_t *inputs, unsigned n_inputs, const ts_metrics_t *tsm, cas_t *cas);
int run_output(tvsrc_t *src, remux_t *rx, out_ctx_t *out, const config_t *cfg, cas_t *cas, metrics_exporter_t *mx, input_metrics_t *im, ts_metrics_t *tsm);

/* single.c */
int tvhead_run_single(const config_t *cfg, metrics_exporter_t *mx);

/* mpts.c */
int tvhead_run_mpts(const config_t *cfg, metrics_exporter_t *mx);

#endif
