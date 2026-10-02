/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <string.h>

#include "../../dash/live.h"
#include "../../hls/live.h"

#include "source_priv.h"

static int has_ts_sync(const unsigned char *b, size_t n) {
  return n >= 3 * 188 && b[0] == 0x47 && b[188] == 0x47 && b[376] == 0x47;
}

http_content_kind_t tssrc_http_classify(const unsigned char *b, size_t n) {
  size_t off = (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
  if (n - off >= 7 && !memcmp(b + off, "#EXTM3U", 7)) return HTTP_CONTENT_HLS;
  if (n - off >= 5 && !memcmp(b + off, "<?xml", 5) && memmem(b, n, "<MPD", 4)) return HTTP_CONTENT_DASH;
  if (has_ts_sync(b, n)) return HTTP_CONTENT_TS;
  return HTTP_CONTENT_UNKNOWN;
}

static void hls_fmp4_emit_pkt(void *ctx, const unsigned char *pkt188) { hls_live_emit((hls_live_t *)ctx, pkt188, 188); }

void tssrc_hls_fmp4_segment_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len) {
  tssrc_media_ctx_t *mc = ctx;
  esbuild_remux_feed(&mc->owner->remux, mc->stream_idx, data, len, hls_fmp4_emit_pkt, h);
}

static void dash_fmp4_emit_pkt(void *ctx, const unsigned char *pkt188) { dash_live_emit((dash_live_t *)ctx, pkt188, 188); }

void tssrc_dash_fmp4_segment_feed(void *ctx, dash_live_t *h, const unsigned char *data, size_t len) {
  tssrc_media_ctx_t *mc = ctx;
  if (!mc->got_init) {
    mc->got_init = 1;
    esbuild_remux_add_init(&mc->owner->remux, mc->stream_idx, data, len);
    return;
  }
  esbuild_remux_feed(&mc->owner->remux, mc->stream_idx, data, len, dash_fmp4_emit_pkt, h);
}

ssize_t tssrc_http_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  unsigned i;
  switch (s->http_sub) {
  case HTTP_SUB_TS:
    if (s->http_sniff_pos < s->http_sniff_len) {
      size_t n = s->http_sniff_len - s->http_sniff_pos;
      if (n > cap) n = cap;
      memcpy(buf, s->http_sniff_buf + s->http_sniff_pos, n);
      s->http_sniff_pos += n;
      return (ssize_t)n;
    }
    return http_read(s->h, buf, cap, reason_out);
  case HTTP_SUB_HLS:
    for (i = 0; i < s->n_media; i++) {
      ssize_t n = hls_live_read(s->hls[i], buf, cap, reason_out);
      if (n != 0) return n;
    }
    return 0;
  case HTTP_SUB_DASH:
    for (i = 0; i < s->n_media; i++) {
      ssize_t n = dash_live_read(s->dash[i], buf, cap, reason_out);
      if (n != 0) return n;
    }
    return 0;
  }
  return -1;
}

int tssrc_http_fd(const tssrc_t *s) {
  unsigned i;
  switch (s->http_sub) {
  case HTTP_SUB_TS:
    return http_fd(s->h);
  case HTTP_SUB_HLS:
    for (i = 0; i < s->n_media; i++) {
      int fd = hls_live_poll_fd(s->hls[i]);
      if (fd >= 0) return fd;
    }
    return -1;
  case HTTP_SUB_DASH:
    for (i = 0; i < s->n_media; i++) {
      int fd = dash_live_poll_fd(s->dash[i]);
      if (fd >= 0) return fd;
    }
    return -1;
  }
  return -1;
}

void tssrc_http_free_media(const tssrc_t *s) {
  for (unsigned i = 0; i < s->n_media; i++) {
    if (s->hls[i]) hls_live_free(s->hls[i]);
    if (s->dash[i]) dash_live_free(s->dash[i]);
  }
}
