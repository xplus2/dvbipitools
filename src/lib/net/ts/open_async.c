/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include "../../dash/mpd.h"
#include "../../sys/ioutil.h"
#include "../../hls/playlist.h"

#include "source_priv.h"

typedef enum {
  HOPEN_SNIFF_CLASSIFY,
  HOPEN_SNIFF_MANIFEST,
  HOPEN_HLS_VARIANT_FETCH,
  HOPEN_HLS_INIT_FETCH
} http_open_phase_t;

struct tssrc_open {
  http_async_t *ha;
  http_t *h;
  http_fetch_t *hf;
  tssrc_t *result;
  int done;

  tssrc_kind_t kind;
  char user_agent[128];
  int insecure;

  http_open_phase_t phase;
  http_content_kind_t content;
  unsigned char buf[TSSRC_HTTP_MANIFEST_CAP];
  size_t buf_len;

  http_url_t media_url[2];
  hls_playlist_t hls_pl[2];
  unsigned n_media_url;
  unsigned fetch_idx;
  unsigned char fetch_buf[TSSRC_HTTP_INIT_CAP];
};

static int http_body_read_step(http_t *h, unsigned char *buf, size_t cap, size_t *len, net_err_reason_t *reason_out) {
  ssize_t n;
  net_err_reason_t r = NET_ERR_OTHER;
  if (*len >= cap) return 2;
  n = http_read(h, buf + *len, cap - *len, &r);
  if (n < 0) {
    if (r == NET_ERR_EOF) return 2;
    if (reason_out) *reason_out = r;
    return -1;
  }
  if (n == 0) return 0;
  *len += (size_t)n;
  return 1;
}

static int dash_find_adaptation_set(const dash_period_t *p, const char *mime_prefix) {
  size_t n = strlen(mime_prefix);
  for (unsigned i = 0; i < p->n_adaptation_sets; i++)
    if (!strncmp(p->adaptation_sets[i].mime_type, mime_prefix, n)) return (int)i;
  return -1;
}

static int build_dash_result(tssrc_open_t *o, const dash_mpd_t *mpd, const http_url_t *mpd_url, net_err_reason_t *reason_out) {
  int video_idx = dash_find_adaptation_set(&mpd->periods[0], "video/");
  int audio_idx = dash_find_adaptation_set(&mpd->periods[0], "audio/");
  tssrc_t *r;

  if (video_idx < 0 && audio_idx < 0) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return 0;
  }
  r = calloc(1, sizeof *r);
  if (!r) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return 0;
  }
  r->kind = TSSRC_HTTP;
  r->http_sub = HTTP_SUB_DASH;

  if (video_idx >= 0) {
    r->media_ctx[r->n_media].owner = r;
    r->media_ctx[r->n_media].stream_idx = r->n_media;
    r->dash[r->n_media] = dash_live_new(mpd_url, o->user_agent, o->insecure, 0, "tssource", (unsigned)video_idx, NULL, tssrc_dash_fmp4_segment_feed, &r->media_ctx[r->n_media]);
    if (!r->dash[r->n_media]) {
      tssrc_close(r);
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return 0;
    }
    r->n_media++;
  }
  if (audio_idx >= 0) {
    r->media_ctx[r->n_media].owner = r;
    r->media_ctx[r->n_media].stream_idx = r->n_media;
    r->dash[r->n_media] = dash_live_new(mpd_url, o->user_agent, o->insecure, 0, "tssource", (unsigned)audio_idx, NULL, tssrc_dash_fmp4_segment_feed, &r->media_ctx[r->n_media]);
    if (!r->dash[r->n_media]) {
      tssrc_close(r);
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return 0;
    }
    r->n_media++;
  }
  o->result = r;
  return 1;
}

static int build_hls_passthrough_result(tssrc_open_t *o, net_err_reason_t *reason_out) {
  tssrc_t *r = calloc(1, sizeof *r);
  if (!r) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return 0;
  }
  r->kind = TSSRC_HTTP;
  r->http_sub = HTTP_SUB_HLS;
  r->hls[0] = hls_live_new(&o->media_url[0], o->user_agent, o->insecure, 0, "tssource", NULL, hls_ts_passthrough_feed, &r->ts_pass);
  if (!r->hls[0]) {
    free(r);
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return 0;
  }
  r->n_media = 1;
  o->result = r;
  return 1;
}

tssrc_open_t *tssrc_open_async_start(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  tssrc_open_t *o = calloc(1, sizeof *o);
  if (!o) return NULL;
  o->kind = cfg->kind;
  if (cfg->kind == TSSRC_HTTP) {
    const char *ua = cfg->user_agent ? cfg->user_agent : "dvbipitools";
    bufcpy(o->user_agent, sizeof o->user_agent, ua);
    o->insecure = cfg->insecure_tls;
    o->ha = http_async_start(&cfg->http, ua, cfg->insecure_tls, NULL, reason_out);
    if (!o->ha) {
      free(o);
      return NULL;
    }
    return o;
  }
  o->result = tssrc_open(cfg, reason_out); /* RTP/UDP/STDIN/FILE. cheap, local-only, done synchronously */
  if (!o->result) {
    free(o);
    return NULL;
  }
  o->done = 1;
  return o;
}

int tssrc_open_async_poll_fd(const tssrc_open_t *o) {
  if (o->ha) return http_async_poll_fd(o->ha);
  if (o->hf) return http_fetch_poll_fd(o->hf);
  if (o->h) return http_fd(o->h);
  return -1;
}

short tssrc_open_async_poll_events(const tssrc_open_t *o) {
  if (o->ha) return http_async_poll_events(o->ha);
  if (o->hf) return http_fetch_poll_events(o->hf);
  if (o->h) return POLLIN;
  return 0;
}

typedef enum { HOPEN_STEP_CONTINUE, HOPEN_STEP_PENDING, HOPEN_STEP_ERROR, HOPEN_STEP_DONE } http_open_step_t;

static http_open_step_t step_sniff_classify(tssrc_open_t *o, net_err_reason_t *reason_out) {
  int rc = http_body_read_step(o->h, o->buf, TSSRC_HTTP_SNIFF_CAP, &o->buf_len, reason_out);
  if (rc == 0) return HOPEN_STEP_PENDING;
  if (rc == -1) {
    http_close(o->h);
    o->h = NULL;
    return HOPEN_STEP_ERROR;
  }
  if (rc == 1 && o->buf_len < TSSRC_HTTP_SNIFF_CAP) return HOPEN_STEP_CONTINUE;
  o->content = tssrc_http_classify(o->buf, o->buf_len);
  if (o->content == HTTP_CONTENT_UNKNOWN) {
    http_close(o->h);
    o->h = NULL;
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  if (o->content == HTTP_CONTENT_TS) {
    o->result = calloc(1, sizeof *o->result);
    if (!o->result) {
      http_close(o->h);
      o->h = NULL;
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return HOPEN_STEP_ERROR;
    }
    o->result->kind = TSSRC_HTTP;
    o->result->http_sub = HTTP_SUB_TS;
    o->result->h = o->h;
    memcpy(o->result->http_sniff_buf, o->buf, o->buf_len);
    o->result->http_sniff_len = o->buf_len;
    o->h = NULL;
    o->done = 1;
    return HOPEN_STEP_DONE;
  }
  o->phase = HOPEN_SNIFF_MANIFEST;
  return HOPEN_STEP_CONTINUE;
}

static http_open_step_t step_sniff_manifest(tssrc_open_t *o, net_err_reason_t *reason_out) {
  int rc = http_body_read_step(o->h, o->buf, TSSRC_HTTP_MANIFEST_CAP, &o->buf_len, reason_out);
  size_t term;
  http_url_t base;
  if (rc == 0) return HOPEN_STEP_PENDING;
  if (rc == -1) {
    http_close(o->h);
    o->h = NULL;
    return HOPEN_STEP_ERROR;
  }
  if (rc == 1) return HOPEN_STEP_CONTINUE;
  term = o->buf_len < TSSRC_HTTP_MANIFEST_CAP ? o->buf_len : TSSRC_HTTP_MANIFEST_CAP - 1;
  base = *http_final_url(o->h);
  o->buf[term] = '\0';

  if (o->content == HTTP_CONTENT_DASH) {
    dash_mpd_t mpd;
    int ok = dash_mpd_parse((char *)o->buf, &base, &mpd) && mpd.n_periods > 0;
    if (!ok && reason_out) *reason_out = NET_ERR_FORMAT;
    if (ok) ok = build_dash_result(o, &mpd, &base, reason_out);
    http_close(o->h);
    o->h = NULL;
    if (!ok) {
      if (reason_out && *reason_out == NET_ERR_OTHER) *reason_out = NET_ERR_FORMAT;
      return HOPEN_STEP_ERROR;
    }
    o->done = 1;
    return HOPEN_STEP_DONE;
  }

  if (hls_body_is_master((char *)o->buf)) {
    hls_master_t hm;
    int best = -1;
    int parsed;
    const hls_audio_rendition_t *ar;
    parsed = hls_master_parse((char *)o->buf, &base, &hm);
    if (parsed) best = hls_master_pick_highest(&hm);
    if (!parsed || best < 0) {
      http_close(o->h);
      o->h = NULL;
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return HOPEN_STEP_ERROR;
    }
    if (http_url_parse(hm.variants[(unsigned)best].url, &o->media_url[0])) {
      http_close(o->h);
      o->h = NULL;
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return HOPEN_STEP_ERROR;
    }
    o->n_media_url = 1;
    ar = hls_master_find_audio(&hm, hm.variants[(unsigned)best].audio_group_id);
    if (ar && !http_url_parse(ar->url, &o->media_url[1])) o->n_media_url = 2;
    http_close(o->h);
    o->h = NULL;
    o->fetch_idx = 0;
    o->phase = HOPEN_HLS_VARIANT_FETCH;
    return HOPEN_STEP_CONTINUE;
  }

  o->media_url[0] = base;
  o->n_media_url = 1;
  if (!hls_playlist_parse((char *)o->buf, &base, &o->hls_pl[0])) {
    http_close(o->h);
    o->h = NULL;
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  http_close(o->h);
  o->h = NULL;
  if (!o->hls_pl[0].map_uri[0]) {
    if (!build_hls_passthrough_result(o, reason_out)) return HOPEN_STEP_ERROR;
    o->done = 1;
    return HOPEN_STEP_DONE;
  }
  o->fetch_idx = 0;
  o->phase = HOPEN_HLS_INIT_FETCH;
  return HOPEN_STEP_CONTINUE;
}

static http_open_step_t step_hls_variant_fetch(tssrc_open_t *o, net_err_reason_t *reason_out) {
  http_fetch_state_t st;
  size_t len;
  http_t *reuse;
  if (!o->hf) {
    o->hf = http_fetch_start(&o->media_url[o->fetch_idx], o->user_agent, o->insecure, NULL, NULL, o->fetch_buf, sizeof o->fetch_buf - 1, NULL, reason_out);
    if (!o->hf) return HOPEN_STEP_ERROR;
  }
  st = http_fetch_step(o->hf, reason_out);
  if (st == HTTP_FETCH_PENDING) return HOPEN_STEP_PENDING;
  if (st == HTTP_FETCH_ERROR) {
    http_fetch_free(o->hf);
    o->hf = NULL;
    return HOPEN_STEP_ERROR;
  }
  http_fetch_take(o->hf, &len, NULL, NULL, 0, NULL, &reuse);
  o->hf = NULL;
  if (reuse) http_close(reuse);
  o->fetch_buf[len] = '\0';
  if (!hls_playlist_parse((char *)o->fetch_buf, &o->media_url[o->fetch_idx], &o->hls_pl[o->fetch_idx])) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  o->fetch_idx++;
  if (o->fetch_idx < o->n_media_url) return HOPEN_STEP_CONTINUE;

  if (o->n_media_url == 2 && !(o->hls_pl[0].map_uri[0] && o->hls_pl[1].map_uri[0])) o->n_media_url = 1;
  if (!o->hls_pl[0].map_uri[0]) {
    if (!build_hls_passthrough_result(o, reason_out)) return HOPEN_STEP_ERROR;
    o->done = 1;
    return HOPEN_STEP_DONE;
  }
  o->fetch_idx = 0;
  o->phase = HOPEN_HLS_INIT_FETCH;
  return HOPEN_STEP_CONTINUE;
}

static http_open_step_t step_hls_init_fetch(tssrc_open_t *o, net_err_reason_t *reason_out) {
  http_fetch_state_t st;
  size_t len;
  http_t *reuse;
  if (!o->hf) {
    http_url_t init_url;
    if (http_url_parse(o->hls_pl[o->fetch_idx].map_uri, &init_url)) return HOPEN_STEP_ERROR;
    o->hf = http_fetch_start(&init_url, o->user_agent, o->insecure, NULL, NULL, o->fetch_buf, sizeof o->fetch_buf, NULL, reason_out);
    if (!o->hf) return HOPEN_STEP_ERROR;
  }
  st = http_fetch_step(o->hf, reason_out);
  if (st == HTTP_FETCH_PENDING) return HOPEN_STEP_PENDING;
  if (st == HTTP_FETCH_ERROR) {
    http_fetch_free(o->hf);
    o->hf = NULL;
    return HOPEN_STEP_ERROR;
  }
  http_fetch_take(o->hf, &len, NULL, NULL, 0, NULL, &reuse);
  o->hf = NULL;
  if (reuse) http_close(reuse);

  if (!o->result) {
    o->result = calloc(1, sizeof *o->result);
    if (!o->result) {
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return HOPEN_STEP_ERROR;
    }
    o->result->kind = TSSRC_HTTP;
    o->result->http_sub = HTTP_SUB_HLS;
  }
  if (!esbuild_remux_add_init(&o->result->remux, o->fetch_idx, o->fetch_buf, len)) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  o->result->media_ctx[o->fetch_idx].owner = o->result;
  o->result->media_ctx[o->fetch_idx].stream_idx = o->fetch_idx;
  o->result->hls[o->fetch_idx] = hls_live_new(&o->media_url[o->fetch_idx], o->user_agent, o->insecure, 0, "tssource", NULL, tssrc_hls_fmp4_segment_feed, &o->result->media_ctx[o->fetch_idx]);
  if (!o->result->hls[o->fetch_idx]) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return HOPEN_STEP_ERROR;
  }
  o->result->n_media++;
  o->fetch_idx++;
  if (o->fetch_idx < o->n_media_url) return HOPEN_STEP_CONTINUE;
  o->done = 1;
  return HOPEN_STEP_DONE;
}

tssrc_open_state_t tssrc_open_async_step(tssrc_open_t *o, net_err_reason_t *reason_out) {
  if (o->done) return TSSRC_OPEN_DONE;

  if (o->ha) {
    http_async_state_t st = http_async_step(o->ha, reason_out);
    if (st == HTTP_ASYNC_PENDING) return TSSRC_OPEN_PENDING;
    if (st == HTTP_ASYNC_ERROR) {
      http_async_free(o->ha);
      o->ha = NULL;
      return TSSRC_OPEN_ERROR;
    }
    o->h = http_async_take(o->ha);
    o->ha = NULL;
    o->phase = HOPEN_SNIFF_CLASSIFY;
  }

  if (o->kind != TSSRC_HTTP) return TSSRC_OPEN_DONE;

  for (;;) {
    http_open_step_t r;
    switch (o->phase) {
      case HOPEN_SNIFF_CLASSIFY:    r = step_sniff_classify(o, reason_out); break;
      case HOPEN_SNIFF_MANIFEST:    r = step_sniff_manifest(o, reason_out); break;
      case HOPEN_HLS_VARIANT_FETCH: r = step_hls_variant_fetch(o, reason_out); break;
      case HOPEN_HLS_INIT_FETCH:    r = step_hls_init_fetch(o, reason_out); break;
      default: return TSSRC_OPEN_ERROR;
    }
    switch (r) {
      case HOPEN_STEP_CONTINUE: continue;
      case HOPEN_STEP_PENDING:  return TSSRC_OPEN_PENDING;
      case HOPEN_STEP_ERROR:    return TSSRC_OPEN_ERROR;
      case HOPEN_STEP_DONE:     return TSSRC_OPEN_DONE;
    }
  }
}

tssrc_t *tssrc_open_async_take(tssrc_open_t *o) {
  tssrc_t *r = o->result;
  free(o);
  return r;
}

void tssrc_open_async_free(tssrc_open_t *o) {
  if (!o) return;
  if (o->ha) http_async_free(o->ha);
  if (o->h) http_close(o->h);
  if (o->hf) http_fetch_free(o->hf);
  if (o->result) tssrc_close(o->result);
  free(o);
}
