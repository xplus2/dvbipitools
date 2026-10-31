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

route_result_t dispatch_route_hls(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid,
  const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  seg_container_t container = rt->fmt == ROUTE_FMT_HLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
  route_setup_t rs;
  route_setup_status_t st;
  size_t bytes = 0;
  st = route_setup(rt, &list_num, filter, pmt_pid, lcevc, c->client_ip, 1, &item_bufs, &cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
    container, container == SEG_CONTAINER_FMP4 ? 0.0 : reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_PENDING) return park_open(c, rs.job, keep_alive);
  if (st == ROUTE_SETUP_404) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (st == ROUTE_SETUP_503) {
    respond_status(c, RESP_503, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!strcmp(rt->hls_file, "index.m3u8") && !hls_store_ready(rs.ctx, filter, pmt_pid, lcevc, container) &&
      hls_cold_try_park(c, &(hls_cold_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, HLS_COLD_HLS, container, 0, is_head, keep_alive, origin_hdr,
        hls_cold_timeout_ms(reactor_cfg()->segment_size), rs.ws_handle, NULL})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (hls_serve(c, rs.ctx, filter, pmt_pid, lcevc, container, rt->hls_file, is_head, keep_alive, if_none_match, origin_hdr, &bytes))
    ws_clients_add_bytes(rs.ws_handle, bytes);
  else
    respond_status(c, RESP_404, keep_alive);
  return ROUTE_RESULT_FINISH;
}

route_result_t dispatch_route_llhls(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive,
  const char *if_none_match, const char *origin_hdr, const char *query) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  seg_container_t container = rt->fmt == ROUTE_FMT_LLHLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
  uint32_t want_seg;
  int want_part;
  route_setup_t rs;
  route_setup_status_t st;
  size_t bytes = 0;
  st = route_setup(rt, &list_num, filter, pmt_pid, lcevc, c->client_ip, 1, &item_bufs, &cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
    container, reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_PENDING) return park_open(c, rs.job, keep_alive);
  if (st == ROUTE_SETUP_404) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (st == ROUTE_SETUP_503) {
    respond_status(c, RESP_503, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!strcmp(rt->hls_file, "index_ll.m3u8") && !hls_ll_store_ready(rs.ctx, filter, pmt_pid, lcevc, container) && hls_cold_try_park(c, &(hls_cold_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, HLS_COLD_LLHLS, container, 0, is_head, keep_alive, origin_hdr,
      hls_cold_timeout_ms(reactor_cfg()->segment_size), rs.ws_handle, NULL})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (!strcmp(rt->hls_file, "index_ll.m3u8") && parse_blocking_reload(query, &want_seg, &want_part) &&
      !hls_part_available(rs.ctx, filter, pmt_pid, lcevc, container, want_seg, want_part) &&
      llhls_try_park(c, &(llhls_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, is_head, keep_alive, NULL, origin_hdr, want_seg, want_part, (int)(reactor_cfg()->hls_part_size * 2000.0), rs.ws_handle, container})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (hls_serve_ll(c, rs.ctx, filter, pmt_pid, lcevc, container, rt->hls_file, is_head, keep_alive, if_none_match, origin_hdr, &bytes)) {
    ws_clients_add_bytes(rs.ws_handle, bytes);
  } else {
    respond_status(c, RESP_404, keep_alive);
  }
  return ROUTE_RESULT_FINISH;
}

route_result_t dispatch_route_dash(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *origin_hdr,
                                          const char *host_hdr) {
  char utc_url[TIME_EP_URL_MAX];
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  int want_ll = rt->fmt == ROUTE_FMT_LLDASH;
  route_setup_t rs;
  route_setup_status_t st;
  size_t bytes = 0;
  time_ep_utc_url(utc_url, sizeof utc_url, reactor_cfg()->dash_utc_url, c->ssl != NULL, host_hdr);
  st = route_setup(rt, &list_num, filter, pmt_pid, lcevc, c->client_ip, 1, &item_bufs, &cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
                   SEG_CONTAINER_FMP4, want_ll ? reactor_cfg()->dash_part_size : 0.0, &rs);
  if (st == ROUTE_SETUP_PENDING) return park_open(c, rs.job, keep_alive);
  if (st == ROUTE_SETUP_404) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (st == ROUTE_SETUP_503) {
    respond_status(c, RESP_503, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (strcmp(rt->hls_file, "manifest.mpd") != 0) {
    if (!reactor_cfg()->no_lldash && !is_head && dash_lldash_try_attach(c, rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, keep_alive, origin_hdr, rs.ws_handle))
      return ROUTE_RESULT_FINISH;
    if (dash_serve_seg(c, rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, is_head, keep_alive, origin_hdr, &bytes))
      ws_clients_add_bytes(rs.ws_handle, bytes);
    else
      respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!hls_store_ready(rs.ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4) &&
      hls_cold_try_park(c, &(hls_cold_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, HLS_COLD_DASH, SEG_CONTAINER_FMP4, want_ll, is_head, keep_alive, origin_hdr, hls_cold_timeout_ms(reactor_cfg()->segment_size), rs.ws_handle, utc_url})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (dash_serve(c, rs.ctx, filter, pmt_pid, lcevc, want_ll, utc_url, is_head, keep_alive, origin_hdr, &bytes))
    ws_clients_add_bytes(rs.ws_handle, bytes);
  else
    respond_status(c, RESP_404, keep_alive);
  return ROUTE_RESULT_FINISH;
}

route_result_t dispatch_route_mp4(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *origin_hdr) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  char mp4_head_header[160];
  size_t mp4_head_len = build_stream_header(mp4_head_header, sizeof mp4_head_header, "video/mp4", c->ssl != NULL);
  int wsh;
  capture_ctx_t *ctx;
  capture_open_job_t *job;
  open_res_t res = open_source_try(rt, &list_num, &ctx, &job);
  if (res == OPEN_RES_PENDING) return park_open(c, job, keep_alive);
  if (res == OPEN_RES_FAIL) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (is_head) {
    capture_close(ctx);
    conn_queue(c, mp4_head_header, mp4_head_len);
    return ROUTE_RESULT_FINISH;
  }
  route_client_info(rt, list_num, filter, pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
  wsh = ws_clients_touch(&cinfo);
  if (wsh < 0) {
    capture_close(ctx);
    respond_status(c, RESP_503, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!hls_seg_touch(ctx, filter, pmt_pid, lcevc, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_FMP4, 0.0)) {
    respond_status(c, RESP_503, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!hls_store_ready(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4) &&
      hls_cold_try_park(c, &(hls_cold_park_req_t){ctx, filter, pmt_pid, lcevc, "", HLS_COLD_MP4, SEG_CONTAINER_FMP4, 0, is_head, keep_alive, origin_hdr, hls_cold_timeout_ms(reactor_cfg()->segment_size), wsh, NULL})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (!mp4push_try_attach(c, ctx, filter, pmt_pid, lcevc, wsh)) {
    respond_status(c, RESP_503, keep_alive);
  }
  return ROUTE_RESULT_FINISH;
}
