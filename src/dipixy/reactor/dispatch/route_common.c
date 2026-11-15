/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "route_common.h"
#include "../../core/time_ep.h"

typedef enum { ROUTE_SETUP_OK, ROUTE_SETUP_404, ROUTE_SETUP_503, ROUTE_SETUP_PENDING } route_setup_status_t;

typedef struct {
  capture_ctx_t *ctx;
  int ws_handle;
  capture_open_job_t *job;
} route_setup_t;

/* ctx already closed on !OK */
static route_setup_status_t route_setup(const route_io_t *io, const stream_key_t *want, double part_target, route_setup_t *out) {
  unsigned list_num;
  switch (open_source_try(io->rt, &list_num, &out->ctx, &out->job)) {
    case OPEN_RES_OK:
      break;
    case OPEN_RES_PENDING:
      return ROUTE_SETUP_PENDING;
    case OPEN_RES_FAIL:
      return ROUTE_SETUP_404;
  }
  route_client_info(io->rt, list_num, want->filter, want->pmt_pid, io->client_ip, io->http_ver, io->item_bufs, io->cinfo);
  out->ws_handle = ws_clients_touch(io->cinfo);
  if (out->ws_handle < 0) {
    capture_close(out->ctx);
    return ROUTE_SETUP_503;
  }
  if (!hls_seg_touch(&(stream_key_t){out->ctx, want->filter, want->pmt_pid, want->lcevc, want->container}, reactor_cfg()->segment_size, reactor_cfg()->segment_count, part_target))
    return ROUTE_SETUP_503;
  return ROUTE_SETUP_OK;
}

static double route_part_target(route_fmt_t fmt) {
  const config_t *cfg = reactor_cfg();
  switch (fmt) {
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_LLHLS:
    case ROUTE_FMT_LLHLS_FMP4: return cfg->hls_part_size;
    case ROUTE_FMT_LLDASH: return cfg->dash_part_size;
    default: return 0.0;
  }
}

route_result_t route_dispatch_store(const route_io_t *io) {
  const config_t *cfg = reactor_cfg();
  const route_t *rt = io->rt;
  const route_file_t *f = &rt->file;
  int ts = rt->fmt == ROUTE_FMT_HLS || rt->fmt == ROUTE_FMT_LLHLS;
  stream_key_t key = {NULL, io->filter, io->pmt_pid, io->lcevc, ts ? SEG_CONTAINER_TS : SEG_CONTAINER_FMP4};
  hls_cold_park_req_t cold;
  const hls_store_t *store;
  route_setup_t rs;
  char utc_url[TIME_EP_URL_MAX];
  uint32_t want_seg;
  int want_part;
  size_t bytes;

  switch (route_setup(io, &key, route_part_target(rt->fmt), &rs)) {
    case ROUTE_SETUP_OK:
      break;
    case ROUTE_SETUP_PENDING:
      if (io->ops->park_open(io->t, rs.job)) return ROUTE_RESULT_OPENING;
      capture_open_job_cancel(rs.job);
      io->ops->status(io->t, 503);
      return ROUTE_RESULT_FINISH;
    case ROUTE_SETUP_404:
      io->ops->status(io->t, 404);
      return ROUTE_RESULT_FINISH;
    case ROUTE_SETUP_503:
      io->ops->status(io->t, 503);
      return ROUTE_RESULT_FINISH;
  }
  key.ctx = rs.ctx;
  store = hls_store_find(&key);
  cold = (hls_cold_park_req_t){key, f, HLS_COLD_HLS, 0, io->is_head, io->keep_alive, io->origin, hls_cold_timeout_ms(cfg->segment_size), rs.ws_handle, NULL};
  switch (rt->fmt) {
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4:
      if (f->kind == ROUTE_FILE_INDEX && !hls_store_ready_at(store) && io->ops->cold_park(io->t, &cold)) return ROUTE_RESULT_PARKED;
      bytes = io->ops->hls(io->t, &key, store, f, 0);
      break;
    case ROUTE_FMT_LLHLS:
    case ROUTE_FMT_LLHLS_FMP4:
      cold.kind = HLS_COLD_LLHLS;
      if (f->kind == ROUTE_FILE_INDEX_LL && !hls_ll_store_ready_at(store) && io->ops->cold_park(io->t, &cold)) return ROUTE_RESULT_PARKED;
      if (f->kind == ROUTE_FILE_INDEX_LL && parse_blocking_reload(io->query, &want_seg, &want_part) && !hls_part_available_at(store, want_seg, want_part) &&
          io->ops->ll_park(io->t, &(llhls_park_req_t){key, f, io->is_head, io->keep_alive, io->park_inm, io->origin, want_seg, want_part, (int)(cfg->hls_part_size * 2000.0), rs.ws_handle}))
        return ROUTE_RESULT_PARKED;
      bytes = io->ops->hls(io->t, &key, store, f, 1);
      break;
    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH:
      if (f->kind != ROUTE_FILE_MPD) {
        if (!cfg->no_lldash && !io->is_head && io->ops->lldash(io->t, &key, f, rs.ws_handle)) return ROUTE_RESULT_FINISH;
        bytes = io->ops->dash_seg(io->t, &key, f);
        break;
      }
      time_ep_utc_url(utc_url, sizeof utc_url, cfg->dash_utc_url, io->tls, io->host);
      cold.kind = HLS_COLD_DASH;
      cold.want_ll = rt->fmt == ROUTE_FMT_LLDASH;
      cold.utc_url = utc_url;
      if (!hls_store_ready_at(store) && io->ops->cold_park(io->t, &cold)) return ROUTE_RESULT_PARKED;
      bytes = io->ops->dash(io->t, &key, store, cold.want_ll, utc_url);
      break;
    default:
      cold.kind = HLS_COLD_MP4;
      cold.file = NULL;
      if (!hls_store_ready_at(store) && io->ops->cold_park(io->t, &cold)) return ROUTE_RESULT_PARKED;
      io->ops->mp4(io->t, &key, rs.ws_handle);
      return ROUTE_RESULT_FINISH;
  }
  if (bytes) ws_clients_add_bytes(rs.ws_handle, bytes);
  return ROUTE_RESULT_FINISH;
}
