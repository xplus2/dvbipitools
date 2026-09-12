/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* shared by http2/ and http3/, neither depends on other */

#ifndef DIPIXY_HTTPNG_H
#define DIPIXY_HTTPNG_H

#if defined(HAVE_HTTP2) || defined(HAVE_HTTP3)

#include "../reactor/internal.h"
#include "../ts/pidfilter.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* decimal ASCII, no leading zeros. returns digit count written */
size_t httpng_u64_to_dec(char *buf, uint64_t v);

/* hls_resp_t.status is always one of these */
const char *httpng_status_str(int status);

/* quotes etag into etag_buf (cap etag_buf_sz), clamped to fit. 0 if etag
   NULL/empty, else nv value length (elen+2 quotes) */
size_t httpng_format_etag(char *etag_buf, size_t etag_buf_sz, const char *etag);

/* cb_on_header (h2) / cb_h3_recv_header (h3): both feed same six pseudo/regular
   headers into per-request fields, only struct layout differs */
void httpng_parse_known_header(const char *name, size_t namelen, const char *value, size_t valuelen,
                               char *method, size_t method_sz, char *path, size_t path_sz,
                               char *inm, size_t inm_sz, char *origin, size_t origin_sz,
                               char *authz, size_t authz_sz, char *protocol, size_t protocol_sz);

/* h2_ws_flush/h3_ws_flush shared buffer handoff: swaps locked pend buf -> empty.
   1: out_data/out_len now caller's (send, then resume stream, protocol-specific).
   0: freed */
int httpng_ws_drain_pending(pthread_mutex_t *lock, uint8_t **pending, size_t *pending_len, size_t *pending_cap, uint8_t **out_data, size_t *out_len);

/* h2_hls_cold_finish/h3_hls_cold_finish non-MP4 render dispatch
   returns hls_render()/hls_render_ll()/dash_render() handled flag */
int httpng_hls_cold_render(hls_cold_kind_t kind, seg_container_t container, int want_ll, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc,
                           const char *filename, int is_head, hls_resp_t *out);

typedef struct {
  int proto;
  void (*respond_status)(void *conn, void *req, const char *status);
  void (*respond_401)(void *conn, void *req);
  void (*respond_hls)(void *conn, void *req, int handled, const hls_resp_t *resp, const char *origin_hdr);
  void (*ws_dispatch)(void *conn, void *req);
  int (*admission_ok)(void *conn); /* TS-push slot-reservation guard, keeps WS reachable */
  int (*tspush_dispatch)(void *conn, void *req, int sub);
  int (*dashchunk_dispatch)(void *conn, void *req, int sub, int ws_handle);
  int (*mp4push_dispatch)(void *conn, void *req, int sub, int ws_handle);
  int (*hls_cold_try_park)(void *conn, void *req, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, hls_cold_kind_t kind,
                           seg_container_t container, int want_ll, int is_head, const char *origin_hdr, int timeout_ms, int ws_handle);
  int (*llhls_try_park)(void *conn, void *req, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head, const char *inm,
                        const char *origin_hdr, uint32_t want_seg, int want_part, int timeout_ms, int ws_handle);
} httpng_ops_t;

/* method/path/protocol: request-line/pseudo-headers, never NULL.
   inm/origin/authz: empty string if no header. path mutated in place */
typedef struct {
  char *method;
  char *path;
  const char *inm;
  const char *origin;
  const char *authz;
  const char *protocol;
} httpng_req_hdrs_t;

/* fd: h2 ts_push_subscribe() needs conn fd for ring wake lookups.
   h3: subscriber to conn/stream_id after subscribe (-1) */
void httpng_dispatch(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *client_ip, int fd);

#endif /* HAVE_HTTP2 || HAVE_HTTP3 */
#endif /* DIPIXY_HTTPNG_H */
