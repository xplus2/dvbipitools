/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* internal types/helpers shared by http2.c, http2_tspush.c, http2_hls.c. not public API */

#ifndef DIPIXY_HTTP2_INT_H
#define DIPIXY_HTTP2_INT_H

#ifdef HAVE_HTTP2

#include "../ts/capture/capture.h"
#include "../ts/pidfilter.h"
#include "../reactor/conn.h"
#include "../reactor/internal.h"

#include <nghttp2/nghttp2.h>

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../ws/ws_frame.h"

#define H2_MAX_STREAMS 32 /* concurrent in-flight requests per conn */
#define H2_PATH_MAX 512
#define H2_TSPUSH_MAX 4 /* concurrent TS push streams per H2 connection */
#define H2_DASHCHUNK_MAX 4 /* concurrent LL-DASH chunk streams per H2 connection */
#define H2_WS_MAX 4 /* concurrent WS streams per H2 connection */

/* per-stream state: only method/path needed, dipixy has no req body or fancy headers */
typedef struct {
  int32_t id; /* 0 = slot unused */
  char method[16];
  char path[H2_PATH_MAX];
  char inm[80]; /* if-none-match header value, empty if absent */
  char origin[128]; /* origin header value, empty if absent */
  char protocol[16]; /* :protocol pseudo-header, RFC8441 extended CONNECT */
  char authz[200]; /* authorization header value, empty if absent */
  int dispatch_pending; /* complete request ready to route */
} h2_stream_t;

typedef struct {
  int32_t sid; /* 0 = slot unused */
  int sub_idx; /* ts_sub_t index, read_cb pulls straight from its h2_ring */
} h2_tspush_stream_t;

typedef struct {
  int32_t sid; /* 0 = slot unused */
  int sub_idx; /* dash/lldash.c subscriber index */
} h2_dashchunk_stream_t;

typedef struct {
  int32_t sid;        /* 0 = slot unused */
  conn_t *c;
  ws_parser_t parser;
  pthread_mutex_t pending_lock; /* zero-init valid: default glibc mutex */
  uint8_t *pending;   /* under pending_lock */
  size_t pending_len;
  size_t pending_cap;
  int inflight;
  uint8_t *send_data;
  size_t send_len;
  size_t send_off;
} h2_ws_stream_t;

typedef struct h2_conn {
  int fd;
  nghttp2_session *ng;
  h2_stream_t streams[H2_MAX_STREAMS];
  int done;
  conn_t *c; /* owning conn_t (for out_lock and epfd) */
  h2_tspush_stream_t tspush[H2_TSPUSH_MAX];
  h2_dashchunk_stream_t dashchunk[H2_DASHCHUNK_MAX];
  h2_ws_stream_t ws[H2_WS_MAX];
} h2_conn_t;

#define MAKE_NV_LIT(k, v) {(uint8_t *)(k), (uint8_t *)(v), sizeof(k)-1, sizeof(v)-1, NGHTTP2_NV_FLAG_NONE}

/* from http2.c */
void h2_flush_tx(h2_conn_t *conn, conn_t *c);

/* from http2_tspush.c */
void h2_tspush_on_stream_close(h2_conn_t *conn, int32_t stream_id);
int h2_tspush_dispatch(h2_conn_t *conn, conn_t *c, int32_t stream_id, int tspush_sub);

/* from http2_dashchunk.c */
void h2_dashchunk_on_stream_close(h2_conn_t *conn, int32_t stream_id);
int h2_dashchunk_dispatch(h2_conn_t *conn, conn_t *c, int32_t stream_id, int sub_idx, int ws_handle);

/* from http2_hls.c. body ownership transfers in: malloc'd or NULL, freed here or by drain callback */
void h2_submit_resp(h2_conn_t *conn, int32_t stream_id, int status, const char *content_type, const char *etag, size_t content_length, uint8_t *body, int zc, const char *origin_hdr);

/* from http2_hls.c: LL-HLS blocking-reload parking, mirrors dispatch.c's llhls_try_park() for H2 streams */
int h2_llhls_try_park(h2_conn_t *conn, const conn_t *c, int32_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter,
                      unsigned pmt_pid, const char *filename, int is_head, const char *inm, const char *origin_hdr, uint32_t want_seg, int want_part, int timeout_ms, int ws_handle);
void h2_llhls_on_stream_close(h2_conn_t *conn, int32_t stream_id);
void h2_llhls_on_conn_close(h2_conn_t *conn);

/* from http2_hls.c */
int h2_hls_cold_try_park(h2_conn_t *conn, int32_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, hls_cold_kind_t kind, hls_container_t container,
                         int want_ll, int is_head, const char *origin_hdr, int timeout_ms, int ws_handle);
void h2_hls_cold_on_stream_close(h2_conn_t *conn, int32_t stream_id);
void h2_hls_cold_on_conn_close(h2_conn_t *conn);

/* from http2_ws.c */
void h2_ws_dispatch(h2_conn_t *conn, conn_t *c, int32_t stream_id);
void h2_ws_data_chunk(h2_conn_t *conn, int32_t stream_id, const uint8_t *data, size_t len);
void h2_ws_flush(h2_conn_t *conn);
void h2_ws_on_stream_close(h2_conn_t *conn, int32_t stream_id);
void h2_ws_on_conn_close(h2_conn_t *conn);

#endif /* HAVE_HTTP2 */
#endif /* DIPIXY_HTTP2_INT_H */
