/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_TVHEAD_PRIV_H
#define DIPITVHEAD_TVHEAD_PRIV_H

#include <stdint.h>

#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/metrics/export.h"
#include "lib/mux/fec2022.h"
#include "lib/mux/rtpheader.h"
#include "lib/net/multicast.h"
#include "lib/net/rist/ristout.h"
#include "lib/net/srt/srtsink.h"
#include "lib/tsinspect/inspect.h"

#include "../cas/cas.h"
#include "../input/source.h"
#include "../mux/bitrate.h"
#include "../mux/pcrclock.h"
#include "../mux/remux.h"
#include "tvhead.h"

/* how long to watch PAT-listed PMT candidates before giving up */
#define DISCOVERY_TIMEOUT_S 8.0
#define TS_PER_DGRAM 7

typedef struct {
  tspack_t pz;
  int listed, checked_pmt_pid;
  tsinspect_t *insp;
} discover_state_t;

typedef struct {
  unsigned pid;
  unsigned char cc;
  int active;
  int have_last;
  uint64_t last_pcr;
} out_pcr_pid_t;

typedef struct {
  mcast_t *mc; /* NULL unless -m given */
  int rtp;
  rtpheader_t *rtph;
  mcast_t *fec_mc;
  fec2022_enc_t *fec_enc;
  ristout_t *rist; /* NULL unless -R rist:// given (bonded peers), sent alongside mc if both present */
  srtsink_t *srt;  /* NULL unless -R srt:// given (bonded peers), sent alongside mc if both present */
  bitrate_pacer_t *pacer;
  unsigned char batch[12 + TS_PER_DGRAM * 188]; /* [0,12): RTP header headroom, unused if !rtp */
  int batch_count;
  double batch_open_time;
  int mc_had_error;   /* edge-log gate; a send failure here never stops process */
  int rist_had_error; /* edge-log gate; a write failure here never stops process */
  int srt_connected;  /* edge-log gate for connect/link-down transitions */
  unsigned long long packets;
  unsigned long long errors;
  tsinspect_t *insp;
  pcr_mode_t pcr_mode;
  pcrclock_t pcr_clock;
  uint64_t pcr_pkt_ticks;
  uint64_t pcr_start_index;
  int pcr_latched;
  out_pcr_pid_t pcr_pids[ARGS_MAX_INPUTS];
  unsigned long long pcr_rewritten;
  unsigned long long pcr_injected;
} out_ctx_t;

typedef struct {
  remux_t *rx;
  out_ctx_t *out;
  double now;
  ts_metrics_t *tsm;
  tsinspect_t *insp;
} feed_ctx_t;

/* discover.c */
void print_discovered(const psi_t *psi);
int discover_step(discover_state_t *ds, tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im);
int discover(tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im);
int discover_insp(tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im, tsinspect_t *insp);

/* output.c */
/* caller only calls this when cfg->n_rist > 0; NULL on err */
ristout_t *tvhead_rist_open(const config_t *cfg);
/* caller only calls this when cfg->n_srt > 0; NULL on err */
srtsink_t *tvhead_srt_open(const config_t *cfg);
int tvhead_output_open(const config_t *cfg, out_ctx_t *o);
void tvhead_output_close(out_ctx_t *o);
/* ticks o->srt connect/reconnect + flush. no-op if !o->srt. call every loop iter. */
void tvhead_srt_service(out_ctx_t *o);
void flush_batch(out_ctx_t *o);
void flush_batch_if_stale(out_ctx_t *o);
void packet_cb(void *ctx, const unsigned char *pkt188);
void send_null_packet(out_ctx_t *o);
void out_pcr_pid_set(out_ctx_t *o, unsigned slot, unsigned pid);
int out_pcr_clock(void *ctx, uint64_t *pcr27);
void out_pcr_latch(void *ctx, uint64_t pcr27);
int remux_cb(void *v, const unsigned char *pkt);
int remux_cb_inspect(void *v, const unsigned char *pkt);
void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
  const input_metrics_t *inputs, unsigned n_inputs, const ts_metrics_t *tsm, cas_t *cas);
int run_output(tvsrc_t *src, remux_t *rx, out_ctx_t *out, const config_t *cfg, cas_t *cas, metrics_exporter_t *mx, input_metrics_t *im, ts_metrics_t *tsm, tsinspect_t *insp);

/* single.c */
int tvhead_run_single(const config_t *cfg, metrics_exporter_t *mx);
cas_t *tvhead_single_cas_start(const config_t *cfg, const psi_t *psi, const remux_t *rx);

/* mpts.c */
int tvhead_run_mpts(const config_t *cfg, metrics_exporter_t *mx);

#endif
