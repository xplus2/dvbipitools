/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "../../version.h"
#include "../playlist.h"
#include "priv.h"

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
  metaint_hdr = http_header(h, "icy-metaint");
  metaint = metaint_hdr ? strtoul(metaint_hdr, NULL, 10) : 0;
  s->id3 = id3_new(metaint ? NULL : cb, ctx); /* ICY authoritative, ID3 would alternate with it */
  if (!s->id3) {
    http_close(h);
    free(s);
    return NULL;
  }
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

static int hls_init_feed(void *ctx, const hls_live_t *h, const unsigned char *data, size_t len) {
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
        log_code_ansi("RW400198", "input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mHLS segment format not recognized, skipped\e[0m", s->idx, s->label ? s->label : "?");
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
  source_open_t *o = source_open_async_start(uri, idx, label, insecure, cb, ctx, si, reason_out);
  if (!o) return NULL;
  for (;;) {
    source_open_state_t st = source_open_async_step(o, reason_out);
    if (st == SOURCE_OPEN_DONE) return source_open_async_take(o);
    if (st == SOURCE_OPEN_ERROR) {
      source_open_async_free(o);
      return NULL;
    }
    struct pollfd p = {.fd = source_open_async_poll_fd(o), .events = source_open_async_poll_events(o)};
    poll(&p, 1, 1000);
  }
}
