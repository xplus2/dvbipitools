/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_SOURCE_PRIV_H
#define DIPIRADIOHEAD_INPUT_SOURCE_PRIV_H

#include "lib/net/httpclient/httpclient.h"

#include "lib/demux/rawaudio.h"
#include "lib/demux/tspack.h"
#include "lib/hls/live.h"
#include "lib/mux/esbuild/remux.h"

#include "../../framer/aac_latm.h"
#include "../framequeue.h"
#include "../icy.h"
#include "../id3.h"
#include "../source.h"

#define SRC_BUF_CAP 16384
#define SRC_SNIFF_CAP 2048
#define SRC_MAX_HOPS 5
#define SRC_STALL_TIMEOUT_S 20.0
#define SRC_OPEN_TIMEOUT_S 30.0
#define SRC_REDETECT_FAILS 4

struct source {
  unsigned idx;
  const char *label;
  http_t *http;    /* NULL: HLS-backed */
  hls_live_t *hls; /* NULL: plain stream via http */
  tspack_t hls_tspack;
  rawaudio_demux_t *hls_demux;
  int hls_warned;
  esbuild_remux_t *hls_remux; /* fMP4 segments, allocated on first init segment */
  icy_t *icy; /* NULL: no icy-metaint, ID3-only metadata */
  id3_t *id3;

  int codec_known;
  source_codec_t codec;
  aac_latm_t *latm;
  unsigned resync_fails;   /* consecutive bad frames, codec re-detected at SRC_REDETECT_FAILS */
  size_t detect_skipped;   /* bytes dropped without a confirmed sync */
  unsigned char buf[SRC_BUF_CAP];
  size_t buf_len;
  size_t tag_skip; /* oversize ID3 tag bytes still to discard */
  size_t pending_consume; /* last returned frame's byte count, dropped next call */
  unsigned long long bytes_total;
  double last_rx; /* mono_seconds() of last wire data */

  framequeue_t *fq; /* NULL: no de-jitter, frames go straight out */
  unsigned prefill_ms;
  int fq_running;   /* 0: filling to prefill_ms before release */
  int fq_resumed;   /* set on release (re)start */
  tsinspect_t **insp_slot;
};

/* h absorbed either way: closed on failure, owned by returned source_t on success */
source_t *build_source(http_t *h, unsigned idx, const char *label, const unsigned char *sniff, size_t got, source_meta_cb cb, void *ctx);

/* NULL on OOM */
source_t *build_hls_source(const http_url_t *playlist_url, unsigned idx, const char *label, int insecure, source_meta_cb cb, void *ctx, const source_insp_t *si);

#endif
