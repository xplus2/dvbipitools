/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/dash/dash.h"
#include "dipixy/dash/dash_int.h"

/* "dsegTTTT.m4s" muxed, "dsegvTTTT.m4s" video track, "dsegaTTTT.m4s" audio track. 1 ok (t, container set), 0 wrong shape */
int parse_dash_seg_filename(const char *fn, uint64_t *t, seg_container_t *container) {
  const char *p;
  char *end;
  if (strncmp(fn, "dseg", 4) != 0) return 0;
  p = fn + 4;
  *container = SEG_CONTAINER_FMP4;
  if (*p == 'v') {
    *container = SEG_CONTAINER_FMP4_VIDEO;
    p++;
  } else if (*p == 'a') {
    *container = SEG_CONTAINER_FMP4_AUDIO;
    p++;
  }
  if (*p < '0' || *p > '9') return 0;
  *t = strtoull(p, &end, 10);
  return !strcmp(end, ".m4s");
}

/* "dinitv.mp4", "dinita.mp4": track store init segments */
static int parse_dash_init_filename(const char *fn, seg_container_t *container) {
  if (!strcmp(fn, "dinitv.mp4")) *container = SEG_CONTAINER_FMP4_VIDEO;
  else if (!strcmp(fn, "dinita.mp4")) *container = SEG_CONTAINER_FMP4_AUDIO;
  else return 0;
  return 1;
}

/* NULL if no segment starts exactly at t_ms */
static const hls_seg_t *find_seg_by_time(const hls_snapshot_t *snap, uint64_t t_ms) {
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->ring->segs[(snap->head + i) % HLS_MAX_SEGS];
    if (seg->start_ms == t_ms) return seg;
  }
  return NULL;
}

static int dash_resolve_seg(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, dash_seg_resolve_t *r) {
  const hls_store_t *s;
  const hls_snapshot_t *snap;
  const hls_seg_t *seg;
  seg_container_t container;
  uint64_t req_t;
  int init;

  memset(r, 0, sizeof *r);
  init = parse_dash_init_filename(filename, &container);
  if (!init && !parse_dash_seg_filename(filename, &req_t, &container)) return 0;
  r->content_type = container == SEG_CONTAINER_FMP4_AUDIO ? "audio/mp4" : "video/mp4";
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  if (init) {
    if (!snap || !snap->init_size) {
      r->status = 404;
      return 1;
    }
    r->status = 200;
    r->body = snap->init_data;
    r->body_len = snap->init_size;
    init_etag(snap->init_gen, snap->init_size, r->etag, sizeof r->etag);
    return 1;
  }
  seg = snap ? find_seg_by_time(snap, req_t) : NULL;
  if (!seg) {
    r->status = 404;
    return 1;
  }
  r->status = 200;
  r->body = seg->data;
  r->body_len = seg->size;
  seg_etag(seg->seq, seg->size, r->etag, sizeof r->etag);
  return 1;
}

int dash_serve_seg(conn_t *c, const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes) {
  char cors_hdr[192];
  dash_seg_resolve_t r;
  if (!dash_resolve_seg(ctx, filter, pmt_pid, lcevc, filename, &r)) return 0;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  if (r.status == 404) {
    respond_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  if (hls_zc_eligible(c, r.body_len, is_head)) {
    seg_buf_ref(r.body);
    queue_segment_zc(c, r.body, r.body_len, r.content_type, r.etag, keep_alive, cors_hdr);
  } else {
    queue_segment(c, r.body, r.body_len, r.content_type, r.etag, is_head, keep_alive, cors_hdr);
  }
  if (out_bytes) *out_bytes = r.body_len;
  return 1;
}

int dash_render_seg(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head, hls_resp_t *out) {
  dash_seg_resolve_t r;
  memset(out, 0, sizeof *out);
  if (!dash_resolve_seg(ctx, filter, pmt_pid, lcevc, filename, &r)) return 0;
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  resp_set_zc(out, 200, r.content_type, r.etag, r.body, r.body, r.body_len, is_head);
  return 1;
}
