/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "live.h"

#include <stdlib.h>
#include <string.h>

#include "lib/demux/rawaudio.h"
#include "lib/demux/tspack.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "playlist.h"

#define HLS_ETAG_MAX 128
#define HLS_PLAYLIST_BUF_CAP (64 * 1024)
#define HLS_SEGMENT_BUF_CAP (512 * 1024)
#define HLS_OUT_BUF_CAP (256 * 1024)
#define HLS_OUT_BUF_PREFETCH_HEADROOM (96 * 1024)
#define HLS_DEFAULT_TARGET_DURATION 6
#define HLS_MAX_REUSE_AGE_S 15
#define HLS_JOIN_BACK_SEGMENTS 3

typedef enum { HLS_LIVE_IDLE, HLS_LIVE_FETCHING_PLAYLIST, HLS_LIVE_FETCHING_SEGMENT } hls_live_phase_t;

struct hls_live {
  http_url_t playlist_url;
  char user_agent[128];
  int insecure;
  unsigned idx;
  const char *label;

  hls_live_phase_t phase;
  http_fetch_t *fetch;
  http_t *reuse;
  double reuse_established_at;
  double next_poll_at;
  char etag[HLS_ETAG_MAX];

  int have_sequence;
  unsigned long long next_seq;
  hls_playlist_t pl;
  unsigned pending_idx;

  unsigned char playlist_buf[HLS_PLAYLIST_BUF_CAP + 1];
  unsigned char segment_buf[HLS_SEGMENT_BUF_CAP];

  tspack_t tspack;
  rawaudio_demux_t *demux;

  unsigned char out_buf[HLS_OUT_BUF_CAP];
  size_t out_len;
  size_t out_pos;
};

static void hls_emit(void *ctx, const unsigned char *data, size_t len) {
  hls_live_t *h = ctx;
  size_t room = sizeof h->out_buf - h->out_len;
  size_t n = len < room ? len : room;
  memcpy(h->out_buf + h->out_len, data, n);
  h->out_len += n;
  if (n < len) log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;33mHLS audio buffer full, dropped bytes\e[0m", h->idx, h->label ? h->label : "?");
}

static int on_ts_packet(void *ctx, const unsigned char *pkt) {
  rawaudio_demux_feed(((hls_live_t *)ctx)->demux, pkt);
  return 0;
}

hls_live_t *hls_live_new(const http_url_t *playlist_url, const char *user_agent, int insecure, unsigned idx, const char *label) {
  hls_live_t *h = calloc(1, sizeof *h);
  if (!h) return NULL;
  h->playlist_url = *playlist_url;
  bufcpy(h->user_agent, sizeof h->user_agent, user_agent ? user_agent : "dvbipitools");
  h->insecure = insecure;
  h->idx = idx;
  h->label = label;
  h->phase = HLS_LIVE_IDLE;
  h->demux = rawaudio_demux_new(0, NULL, NULL, hls_emit, h);
  if (!h->demux) {
    free(h);
    return NULL;
  }
  return h;
}

void hls_live_free(hls_live_t *h) {
  if (!h) return;
  if (h->fetch) http_fetch_free(h->fetch);
  if (h->reuse) http_close(h->reuse);
  rawaudio_demux_free(h->demux);
  free(h);
}

int hls_live_poll_fd(const hls_live_t *h) { return h->fetch ? http_fetch_poll_fd(h->fetch) : -1; }

short hls_live_poll_events(const hls_live_t *h) { return h->fetch ? http_fetch_poll_events(h->fetch) : 0; }

int hls_live_has_buffered(const hls_live_t *h) {
  if (h->out_pos < h->out_len) return 1;
  if (h->fetch) return 0;
  if (h->pending_idx < h->pl.n_segments) return 1;
  return mono_seconds() >= h->next_poll_at;
}

static http_t *take_reuse_handle(hls_live_t *h) {
  http_t *reuse = h->reuse;
  h->reuse = NULL;
  if (reuse && mono_seconds() - h->reuse_established_at >= HLS_MAX_REUSE_AGE_S) {
    http_close(reuse);
    reuse = NULL;
  }
  if (!reuse) h->reuse_established_at = mono_seconds();
  return reuse;
}

static int start_playlist_fetch(hls_live_t *h) {
  http_t *reuse = take_reuse_handle(h);
  h->fetch = http_fetch_start(&h->playlist_url, h->user_agent, h->insecure, NULL, h->etag[0] ? h->etag : NULL, h->playlist_buf, sizeof h->playlist_buf - 1, reuse, NULL);
  h->phase = HLS_LIVE_FETCHING_PLAYLIST;
  return h->fetch != NULL;
}

static int start_segment_fetch(hls_live_t *h, const char *url) {
  http_url_t u;
  http_t *reuse = take_reuse_handle(h);
  if (http_url_parse(url, &u)) {
    if (reuse) http_close(reuse);
    return 0;
  }
  h->fetch = http_fetch_start(&u, h->user_agent, h->insecure, NULL, NULL, h->segment_buf, sizeof h->segment_buf, reuse, NULL);
  h->phase = HLS_LIVE_FETCHING_SEGMENT;
  return h->fetch != NULL;
}

static double poll_interval(const hls_live_t *h) {
  double d = h->pl.target_duration ? h->pl.target_duration : HLS_DEFAULT_TARGET_DURATION;
  return d / 2;
}

static int handle_playlist_done(hls_live_t *h, net_err_reason_t *reason_out) {
  size_t len;
  int status;
  int truncated;
  char etag[HLS_ETAG_MAX];

  http_fetch_take(h->fetch, &len, &status, etag, sizeof etag, &truncated, &h->reuse);
  h->fetch = NULL;
  h->phase = HLS_LIVE_IDLE;
  if (status == 304 || len == 0) {
    h->next_poll_at = mono_seconds() + poll_interval(h);
    return 1;
  }

  h->playlist_buf[len] = '\0';
  {
    hls_playlist_t newpl;
    if (!hls_playlist_parse((char *)h->playlist_buf, &h->playlist_url, &newpl)) {
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return -1;
    }
    if (etag[0])
      bufcpy(h->etag, sizeof h->etag, etag);
    h->pl = newpl;
    if (!h->have_sequence) {
      if (h->pl.n_segments) {
        unsigned back = h->pl.n_segments < HLS_JOIN_BACK_SEGMENTS ? h->pl.n_segments : HLS_JOIN_BACK_SEGMENTS;
        h->next_seq = h->pl.media_sequence + h->pl.n_segments - back;
      } else {
        h->next_seq = h->pl.media_sequence;
      }
      h->have_sequence = 1;
    } else if (h->next_seq < h->pl.media_sequence) {
      log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;33mHLS segment gap, skipping ahead\e[0m", h->idx, h->label ? h->label : "?");
      h->next_seq = h->pl.media_sequence;
    }
    h->pending_idx = (unsigned)(h->next_seq - h->pl.media_sequence);
    h->next_poll_at = mono_seconds() + poll_interval(h);
  }
  return 1;
}

static int handle_segment_done(hls_live_t *h) {
  size_t len;

  http_fetch_take(h->fetch, &len, NULL, NULL, 0, NULL, &h->reuse);
  h->fetch = NULL;
  h->phase = HLS_LIVE_IDLE;
  h->tspack.acclen = 0;
  tspack_feed(&h->tspack, h->segment_buf, len, on_ts_packet, h);
  h->pending_idx++;
  h->next_seq++;
  return 1;
}

ssize_t hls_live_read(hls_live_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->phase == HLS_LIVE_IDLE && h->pending_idx < h->pl.n_segments) {
    if (sizeof h->out_buf - h->out_len >= HLS_OUT_BUF_PREFETCH_HEADROOM && !start_segment_fetch(h, h->pl.segments[h->pending_idx].url)) {
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return -1;
    }
  } else if (h->phase == HLS_LIVE_IDLE && mono_seconds() >= h->next_poll_at && !start_playlist_fetch(h)) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return -1;
  }

  if (h->phase != HLS_LIVE_IDLE) {
    http_fetch_state_t st = http_fetch_step(h->fetch, reason_out);
    if (st == HTTP_FETCH_ERROR) {
      http_fetch_free(h->fetch);
      h->fetch = NULL;
      return -1;
    }
    if (st == HTTP_FETCH_DONE) {
      if (h->phase == HLS_LIVE_FETCHING_PLAYLIST) {
        if (handle_playlist_done(h, reason_out) < 0) return -1;
      } else {
        handle_segment_done(h);
      }
    }
  }

  if (h->out_pos < h->out_len) {
    size_t n = h->out_len - h->out_pos;
    if (n > cap) n = cap;
    memcpy(buf, h->out_buf + h->out_pos, n);
    h->out_pos += n;
    if (h->out_pos == h->out_len) {
      h->out_pos = 0;
      h->out_len = 0;
    }
    return (ssize_t)n;
  }
  return 0;
}
