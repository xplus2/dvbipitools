/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_RADIOHEAD_PRIV_H
#define DIPIRADIOHEAD_RADIOHEAD_PRIV_H

#include <poll.h>
#include <stdint.h>
#include <time.h>

#include "lib/mux/fec2022.h"
#include "lib/mux/mpts.h"
#include "lib/mux/rtpheader.h"
#include "lib/net/multicast.h"
#include "lib/net/rist/ristout.h"
#include "lib/net/srt/srtsink.h"
#include "lib/tsinspect/inspect.h"

#include "../cas/cas.h"
#include "../input/inputset.h"
#include "../input/source.h"
#include "../mux/tspacketizer.h"
#include "radiohead.h"

#define TS_PER_DGRAM 7

#define TIMELINE_FRAC_BITS 20

/* media clock: 90 kHz ticks, Q20 fixed point. rate-independent */
static inline uint64_t timeline_pts(uint64_t t) {
  return t >> TIMELINE_FRAC_BITS;
}
static inline void timeline_add(uint64_t *t, unsigned samples, unsigned rate) {
  *t += ((uint64_t)samples * 90000ULL << TIMELINE_FRAC_BITS) / rate;
}

#define RADIOHEAD_POLL_MAX_MS 100
#define RADIOHEAD_MAX_FRAMES_PER_TICK 32 /* per input, per tick. caps one input's backlog delaying others */
#define RADIOHEAD_PACE_TOLERANCE_S 0.3

typedef struct {
  mcast_t *mc; /* NULL unless -m given */
  int rtp;
  rtpheader_t *rtph;
  mcast_t *fec_mc;
  fec2022_enc_t *fec_enc;
  ristout_t *rist; /* NULL unless -R rist:// given (bonded peers), sent alongside mc if both present */
  srtsink_t *srt;  /* NULL unless -R srt:// given (bonded peers), sent alongside mc if both present */
  uint64_t cur_pts;
  unsigned char batch[12 + TS_PER_DGRAM * 188]; /* [0,12): RTP header headroom, unused if !rtp */
  int batch_count;
  int mc_had_error;   /* edge-log gate; a send failure here never stops process */
  int rist_had_error; /* edge-log gate; a write failure here never stops process */
  int srt_connected;  /* edge-log gate for connect/link-down transitions */
  unsigned long long packets;
  unsigned long long errors;
  tsinspect_t *insp;
} out_ctx_t;

/* tool-wide, not per-input. matches spec's unlabeled radio_* metric names */
typedef struct {
  unsigned long long frames_total[3]; /* indexed by source_codec_t */
  unsigned long long framing_errors_total;
  unsigned long long metadata_updates_total;
} radio_metrics_t;

typedef struct {
  char artist[256], title[256];
  int dirty;
  radio_metrics_t *rm; /* shared, not owned */
} meta_state_t;

/* radiohead.c */
void meta_cb(void *ctx, const char *artist, const char *title);
/* caller only calls this when cfg->n_rist > 0; NULL on err */
ristout_t *radiohead_rist_open(const config_t *cfg);
/* caller only calls this when cfg->n_srt > 0; NULL on err */
srtsink_t *radiohead_srt_open(const config_t *cfg);
int radiohead_output_open(const config_t *cfg, out_ctx_t *o);
void radiohead_output_close(out_ctx_t *o);
/* ticks o->srt connect/reconnect + flush. no-op if !o->srt. call every loop iter. */
void radiohead_srt_service(out_ctx_t *o);
void flush_batch(out_ctx_t *o);
void packet_cb(void *ctx, const unsigned char *pkt188);
void packet_cb_inspect(void *ctx, const unsigned char *pkt188);
const char *source_codec_name(source_codec_t c);

typedef struct {
  tspacketizer_t **tsp;
  cas_t *cas;
  const config_t *cfg;
  meta_state_t *meta;
  out_ctx_t *out;
  input_metrics_t *im;
  radio_metrics_t *rm;
  int metrics_on;
  metrics_exporter_t *mx;
  uint64_t *timeline;
  double *pace_deadline;
  double start;
  double *last_stat;
  unsigned long long *last_synced_bytes;
} single_tick_t;
int process_single_frame(single_tick_t *tk, source_t *src);

/* metrics.c */
void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
  const input_metrics_t *inputs, unsigned n_inputs, const radio_metrics_t *rm, cas_t *cas);
void radiohead_mpts_set_cas(mpts_t *mpts, cas_t *cas);
size_t mpts_cas_build_cat(void *ctx, unsigned char *out, size_t cap);
int mpts_cas_ecm_due(void *ctx, size_t vendor_idx, double now_s, unsigned char *out, size_t cap, size_t *out_len);
int mpts_cas_next_emm(void *ctx, size_t vendor_idx, unsigned char *out, size_t cap, size_t *out_len);

/* mpts.c */
typedef struct {
  inputset_t *is;
  mpts_t *mpts;
  cas_t *cas;
  const config_t *cfg;
  tspacketizer_t **tsps;
  meta_state_t *metas;
  uint64_t *timeline;
  double *pace_deadline;
  int *was_connected;
  input_metrics_t *input_stats;
  unsigned long long *last_synced_bytes;
  radio_metrics_t *rm;
  out_ctx_t *out;
  int metrics_on;
  double now;
  time_t now_t;
  const unsigned *pfd_slot;
  const struct pollfd *pfds;
  nfds_t npfd;
  uint32_t ready_mask;
} mpts_tick_t;

uint32_t compute_ready_mask(const unsigned *pfd_slot, const struct pollfd *pfds, nfds_t npfd);
int process_input_slot(mpts_tick_t *tk, unsigned i);
int radiohead_run_mpts(const config_t *cfg, metrics_exporter_t *mx);

#endif
