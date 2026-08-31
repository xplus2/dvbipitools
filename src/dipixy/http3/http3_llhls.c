/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../reactor/internal.h"
#include "http3.h"
#include "http3_int.h"

#define H3_LLHLS_WAITERS_MAX 8

static _Thread_local llhls_waiter_t t_h3_llhls_waiters[H3_LLHLS_WAITERS_MAX];
static _Thread_local int t_h3_llhls_waiters_active;

int h3_llhls_try_park(h3_conn_t *conn, int64_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, const char *inm, const char *origin_hdr,
                      uint32_t want_seg, int want_part, int timeout_ms, int ws_handle) {
  return llhls_waiter_pool_try_park(t_h3_llhls_waiters, H3_LLHLS_WAITERS_MAX, &t_h3_llhls_waiters_active, conn,
                                    stream_id, ctx, filter, pmt_pid, filename, is_head, 0, inm, origin_hdr, want_seg, want_part, timeout_ms, ws_handle);
}

void h3_llhls_on_stream_close(h3_conn_t *c, int64_t stream_id) {
  llhls_waiter_pool_close_owner(t_h3_llhls_waiters, H3_LLHLS_WAITERS_MAX, &t_h3_llhls_waiters_active, c, stream_id);
}

void h3_llhls_on_conn_close(h3_conn_t *c) {
  llhls_waiter_pool_close_owner(t_h3_llhls_waiters, H3_LLHLS_WAITERS_MAX, &t_h3_llhls_waiters_active, c, -1);
}

/* h3_req_t may already be gone (freed on stream close): re-look-up, no-op if so */
static void h3_llhls_finish(llhls_waiter_t *w) {
  h3_conn_t *conn = w->owner;
  hls_resp_t resp;
  h3_req_t *r = find_req(conn, w->stream_id);
  int fd;
  if (!r) return;
  if (!hls_render_ll(w->cap_ctx, &w->filter, w->pmt_pid, w->filename, w->is_head, w->inm[0] ? w->inm : NULL, &resp)) {
    h3_respond_status(conn, w->stream_id, "404");
  } else {
    h3_submit_resp(conn, r, resp.status, resp.content_type, resp.etag, resp.body_len, resp.body, resp.zc, w->origin[0] ? w->origin : NULL);
    if (resp.status == 200) ws_clients_add_bytes(w->ws_handle, resp.body_len);
  }
  fd = conn->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd >= 0)
    flush_tx(conn, fd);
}

void h3_llhls_flush_waiters(void) {
  llhls_waiter_pool_flush(t_h3_llhls_waiters, H3_LLHLS_WAITERS_MAX, &t_h3_llhls_waiters_active, h3_llhls_finish);
}

#endif /* HAVE_HTTP3 */
