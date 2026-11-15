/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include "dipixy/reactor/dispatch/priv.h"
#include "dipixy/reactor/dispatch/route_common.h"
#include "dipixy/hls/hls.h"
#include "dipixy/dash/dash.h"
#include "dipixy/dash/lldash.h"
#include "dipixy/segment/segment.h"
#include "dipixy/segment/mp4push.h"
#include "dipixy/ts/lcevcselect.h"
#include "dipixy/ts/ts_push.h"

static route_result_t park_open(conn_t *c, capture_open_job_t *job, int keep_alive) {
  if (!open_try_park(c, job)) {
    capture_open_job_cancel(job);
    respond_status(c, RESP_503, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  c->state = CONN_DISPATCH;
  return ROUTE_RESULT_OPENING;
}

route_result_t dispatch_route_ts(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  char header[160];
  size_t header_len;
  unsigned tp_pmt_pid = rt->fmt == ROUTE_FMT_TS ? 0 : pmt_pid;
  int spts = rt->fmt == ROUTE_FMT_SPTS;
  int rawaudio = rt->fmt == ROUTE_FMT_RAWAUDIO;
  capture_ctx_t *ctx;
  capture_open_job_t *job;
  open_res_t res = open_source_try(rt, &list_num, &ctx, &job);
  int sub;
  if (res == OPEN_RES_PENDING) return park_open(c, job, keep_alive);
  if (res == OPEN_RES_FAIL) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  header_len = build_stream_header(header, sizeof header, rawaudio ? "audio/mpeg" : "video/mp2t", c->ssl != NULL);
  if (is_head) {
    capture_close(ctx);
    conn_queue(c, header, header_len);
    return ROUTE_RESULT_FINISH;
  }
  if (!capture_ctx_bytes(ctx) && reactor_cfg()->ts_startup_timeout_s > 0.0) {
    ts_cold_park_req_t req = {ctx, rt, list_num, filter, tp_pmt_pid, lcevc, spts, rawaudio, keep_alive, (int)(reactor_cfg()->ts_startup_timeout_s * 1000.0)};
    if (ts_cold_try_park(c, &req)) return ROUTE_RESULT_PARKED;
  }
  route_client_info(rt, list_num, filter, tp_pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
  sub = ts_push_subscribe(ctx, filter, CONN_PROTO_H1, c->fd, tp_pmt_pid, spts, rawaudio, &cinfo, lcevc);
  if (sub < 0) {
    capture_close(ctx);
    respond_status(c, RESP_503, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  c->slot = sub;
  if (!rawaudio) conn_queue(c, header, header_len);
  c->next_state = CONN_NEXT_TSPUSH;
  return ROUTE_RESULT_FINISH;
}

typedef struct {
  conn_t *c;
  int is_head;
  int keep_alive;
  const char *inm;
  const char *origin;
} h1_route_t;

static void h1_status(void *t, int code) {
  const h1_route_t *h = t;
  respond_status(h->c, code == 503 ? RESP_503 : RESP_404, h->keep_alive);
}

static int h1_park_open(void *t, capture_open_job_t *job) {
  const h1_route_t *h = t;
  if (!open_try_park(h->c, job)) return 0;
  h->c->state = CONN_DISPATCH;
  return 1;
}

static int h1_cold_park(void *t, const hls_cold_park_req_t *pr) {
  const h1_route_t *h = t;
  if (!hls_cold_try_park(h->c, pr)) return 0;
  h->c->state = CONN_DISPATCH;
  return 1;
}

static int h1_ll_park(void *t, const llhls_park_req_t *pr) {
  const h1_route_t *h = t;
  if (!llhls_try_park(h->c, pr)) return 0;
  h->c->state = CONN_DISPATCH;
  return 1;
}

static size_t h1_served(const h1_route_t *h, int ok, size_t bytes) {
  if (ok) return bytes;
  respond_status(h->c, RESP_404, h->keep_alive);
  return 0;
}

static size_t h1_hls(void *t, const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int ll) {
  const h1_route_t *h = t;
  size_t bytes = 0;
  int ok = ll ? hls_serve_ll(h->c, k, st, f, h->is_head, h->keep_alive, h->inm, h->origin, &bytes) : hls_serve(h->c, k, st, f, h->is_head, h->keep_alive, h->inm, h->origin, &bytes);
  return h1_served(h, ok, bytes);
}

static size_t h1_dash(void *t, const stream_key_t *k, const hls_store_t *st, int want_ll, const char *utc_url) {
  const h1_route_t *h = t;
  size_t bytes = 0;
  return h1_served(h, dash_serve(h->c, k, st, want_ll, utc_url, h->is_head, h->keep_alive, h->origin, &bytes), bytes);
}

static size_t h1_dash_seg(void *t, const stream_key_t *k, const route_file_t *f) {
  const h1_route_t *h = t;
  size_t bytes = 0;
  return h1_served(h, dash_serve_seg(h->c, k, f, h->is_head, h->keep_alive, h->origin, &bytes), bytes);
}

static int h1_lldash(void *t, const stream_key_t *k, const route_file_t *f, int ws_handle) {
  const h1_route_t *h = t;
  return dash_lldash_try_attach(h->c, k, f, h->keep_alive, h->origin, ws_handle);
}

static void h1_mp4(void *t, const stream_key_t *k, int ws_handle) {
  const h1_route_t *h = t;
  if (!mp4push_try_attach(h->c, k, ws_handle)) respond_status(h->c, RESP_503, h->keep_alive);
}

static route_result_t h1_mp4_head(conn_t *c, const route_t *rt, int keep_alive) {
  char header[160];
  size_t header_len = build_stream_header(header, sizeof header, "video/mp4", c->ssl != NULL);
  unsigned list_num;
  capture_ctx_t *ctx;
  capture_open_job_t *job;
  open_res_t res = open_source_try(rt, &list_num, &ctx, &job);
  if (res == OPEN_RES_PENDING) return park_open(c, job, keep_alive);
  if (res == OPEN_RES_FAIL) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  capture_close(ctx);
  conn_queue(c, header, header_len);
  return ROUTE_RESULT_FINISH;
}

route_result_t dispatch_route_store(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *if_none_match,
                                    const char *origin_hdr, const char *query, const char *host_hdr) {
  const route_io_ops_t ops = {h1_status, h1_park_open, h1_cold_park, h1_ll_park, h1_hls, h1_dash, h1_dash_seg, h1_lldash, h1_mp4};
  h1_route_t h = {c, is_head, keep_alive, if_none_match, origin_hdr};
  route_item_bufs_t item_bufs;
  client_info_t cinfo;
  if (rt->fmt == ROUTE_FMT_MP4 && is_head) return h1_mp4_head(c, rt, keep_alive);
  return route_dispatch_store(&(route_io_t){&ops, &h, rt, filter, pmt_pid, lcevc, c->client_ip, 1, is_head, keep_alive, origin_hdr, NULL, query, host_hdr, c->ssl != NULL, &item_bufs, &cinfo});
}
