/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_SOURCE_PRIV_H
#define DIPIRADIOHEAD_INPUT_SOURCE_PRIV_H

#include "lib/net/httpclient/httpclient.h"

#include "../../framer/aac_latm.h"
#include "../hls/live.h"
#include "../icy.h"
#include "../id3.h"
#include "../source.h"

#define SRC_BUF_CAP 16384
#define SRC_SNIFF_CAP 2048
#define SRC_MAX_HOPS 5

struct source {
  unsigned idx;
  const char *label;
  http_t *http;    /* NULL: HLS-backed */
  hls_live_t *hls; /* NULL: plain stream via http */
  icy_t *icy; /* NULL: no icy-metaint, ID3-only metadata */
  id3_t *id3;

  int codec_known;
  source_codec_t codec;
  aac_latm_t *latm;
  unsigned char buf[SRC_BUF_CAP];
  size_t buf_len;
  size_t pending_consume; /* last returned frame's byte count, dropped next call */
  unsigned long long bytes_total;
};

/* h absorbed either way: closed on failure, owned by returned source_t on success */
source_t *build_source(http_t *h, unsigned idx, const char *label, const unsigned char *sniff, size_t got, source_meta_cb cb, void *ctx);

/* NULL on OOM */
source_t *build_hls_source(const http_url_t *playlist_url, unsigned idx, const char *label, int insecure, source_meta_cb cb, void *ctx);

#endif
