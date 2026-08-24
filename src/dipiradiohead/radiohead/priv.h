/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_RADIOHEAD_PRIV_H
#define DIPIRADIOHEAD_RADIOHEAD_PRIV_H

#include <stdint.h>

#include "lib/mux/mpts.h"
#include "lib/mux/rtpheader.h"
#include "lib/net/multicast.h"
#include "lib/net/rist/ristout.h"
#include "lib/net/srt/srtsink.h"

#include "../cas/cas.h"
#include "../input/source.h"
#include "radiohead.h"

#define TS_PER_DGRAM 7

typedef struct {
  mcast_t *mc; /* NULL unless -m given */
  int rtp;
  rtpheader_t *rtph;
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
/* ticks o->srt connect/reconnect + flush. no-op if !o->srt. call every loop iter. */
void radiohead_srt_service(out_ctx_t *o);
void flush_batch(out_ctx_t *o);
void packet_cb(void *ctx, const unsigned char *pkt188);
const char *codec_name(source_codec_t c);

/* metrics.c */
void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
                  const input_metrics_t *inputs, unsigned n_inputs, const radio_metrics_t *rm, cas_t *cas);
void radiohead_mpts_set_cas(mpts_t *mpts, cas_t *cas);

/* mpts.c */
int radiohead_run_mpts(const config_t *cfg, metrics_exporter_t *mx);

#endif
