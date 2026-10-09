/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "../../version.h"
#include "../playlist.h"
#include "priv.h"

static ssize_t sniff_fill(http_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  size_t got = 0;
  int stalls = 0;

  while (got < cap && stalls < 3) {
    ssize_t n = http_read(h, buf + got, cap - got, reason_out);
    if (n < 0) return got > 0 ? (ssize_t)got : -1; /* connection close after a full small (e.g. playlist) body is not an error */
    if (n == 0) {
      stalls++;
      continue;
    }
    got += (size_t)n;
    stalls = 0;
  }
  return (ssize_t)got;
}

source_t *build_source(http_t *h, unsigned idx, const char *label, const unsigned char *sniff, size_t got, source_meta_cb cb, void *ctx) {
  source_t *s = calloc(1, sizeof *s);
  const char *metaint_hdr;
  size_t metaint;
  if (!s) {
    http_close(h);
    return NULL;
  }
  s->idx = idx;
  s->label = label;
  s->last_rx = mono_seconds();
  s->http = h;
  s->id3 = id3_new(cb, ctx);
  if (!s->id3) {
    http_close(h);
    free(s);
    return NULL;
  }
  metaint_hdr = http_header(h, "icy-metaint");
  metaint = metaint_hdr ? strtoul(metaint_hdr, NULL, 10) : 0;
  if (metaint) {
    s->icy = icy_new(metaint, cb, ctx);
    if (!s->icy) {
      id3_free(s->id3);
      http_close(h);
      free(s);
      return NULL;
    }
  }

  if (s->icy) {
    s->buf_len = icy_feed(s->icy, sniff, got, s->buf, sizeof s->buf);
  } else {
    s->buf_len = got < sizeof s->buf ? got : sizeof s->buf;
    memcpy(s->buf, sniff, s->buf_len);
  }
  return s;
}

static void hls_audio_emit(void *ctx, const unsigned char *data, size_t len) {
  source_t *s = ctx;
  hls_live_emit(s->hls, data, len);
}

static int hls_on_ts_packet(void *ctx, const unsigned char *pkt) {
  rawaudio_demux_feed(((source_t *)ctx)->hls_demux, pkt);
  return 0;
}

static void hls_ts_out(void *ctx, const unsigned char *pkt) { hls_on_ts_packet(ctx, pkt); }

static int hls_init_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len) {
  source_t *s = ctx;
  (void)h;
  if (!s->hls_remux) {
    s->hls_remux = malloc(sizeof *s->hls_remux);
    if (!s->hls_remux) return 0;
  }
  return esbuild_remux_init(s->hls_remux, data, len);
}

static void hls_segment_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len) {
  source_t *s = ctx;
  switch (hls_live_segment_kind(data, len)) {
    case HLS_SEG_TS:
      s->hls_tspack.acclen = 0;
      tspack_feed(&s->hls_tspack, data, len, hls_on_ts_packet, s);
      break;
    case HLS_SEG_PACKED_AUDIO:
      hls_live_emit(h, data, len);
      break;
    case HLS_SEG_FMP4:
      if (s->hls_remux) esbuild_remux_feed(s->hls_remux, 0, data, len, hls_ts_out, s);
      break;
    case HLS_SEG_UNKNOWN:
      if (!s->hls_warned) {
        log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mHLS segment format not recognized, skipped\e[0m", s->idx, s->label ? s->label : "?");
        s->hls_warned = 1;
      }
      break;
  }
}

source_t *build_hls_source(const http_url_t *playlist_url, unsigned idx, const char *label, int insecure, source_meta_cb cb, void *ctx, const source_insp_t *si) {
  source_t *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->idx = idx;
  s->label = label;
  s->last_rx = mono_seconds();
  s->insp_slot = si ? si->slot : NULL;
  s->id3 = id3_new(cb, ctx);
  if (!s->id3) {
    free(s);
    return NULL;
  }
  s->hls_demux = rawaudio_demux_new(0, NULL, NULL, hls_audio_emit, s);
  if (!s->hls_demux) {
    id3_free(s->id3);
    free(s);
    return NULL;
  }
  s->hls = hls_live_new(playlist_url, TOOL_NAME "/" TOOL_VERSION, insecure, idx, label, si, hls_segment_feed, s);
  if (!s->hls) {
    rawaudio_demux_free(s->hls_demux);
    id3_free(s->id3);
    free(s);
    return NULL;
  }
  hls_live_set_init_cb(s->hls, hls_init_feed, s);
  return s;
}

source_t *source_open(const char *uri, unsigned idx, const char *label, int insecure, source_meta_cb cb, void *ctx, const source_insp_t *si, net_err_reason_t *reason_out) {
  char cur_uri[2048];
  bufcpy(cur_uri, sizeof cur_uri, uri);
  for (int hops = 0; hops < SRC_MAX_HOPS; hops++) {
    http_url_t u;
    http_t *h;
    unsigned char sniff[SRC_SNIFF_CAP];
    ssize_t got;
    char next[2048];
    if (http_url_parse(cur_uri, &u)) {
      log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31minvalid uri\e[0m: %s", idx, label ? label : "?", cur_uri);
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return NULL;
    }
    h = http_get(&u, TOOL_NAME "/" TOOL_VERSION, insecure, NULL, reason_out);
    if (!h) return NULL;
    if (reason_out) *reason_out = NET_ERR_TIMEOUT; /* default if sniff_fill stalls out without a harder error */
    got = sniff_fill(h, sniff, sizeof sniff, reason_out);
    if (got <= 0) {
      log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mempty response\e[0m from %s", idx, label ? label : "?", cur_uri);
      http_close(h);
      return NULL;
    }

    if (playlist_is_hls_media(sniff, (size_t)got)) {
      const http_url_t *pu = http_final_url(h);
      source_t *s = build_hls_source(pu, idx, label, insecure, cb, ctx, si);
      http_close(h);
      if (!s && reason_out) *reason_out = NET_ERR_OTHER;
      return s;
    }
    if (playlist_extract(sniff, (size_t)got, http_final_url(h), next, sizeof next)) {
      http_close(h);
      bufcpy(cur_uri, sizeof cur_uri, next);
      continue;
    }
    if (reason_out) *reason_out = NET_ERR_OTHER; /* build_source() only fails on allocation */
    return build_source(h, idx, label, sniff, (size_t)got, cb, ctx);
  }
  log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mtoo many playlist redirects\e[0m", idx, label ? label : "?");
  if (reason_out) *reason_out = NET_ERR_FORMAT;
  return NULL;
}
