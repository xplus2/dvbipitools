/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* internal types/helpers shared by http3_quic.c and http3_req.c. not public API */

#ifndef DIPIXY_HTTP3_INT_H
#define DIPIXY_HTTP3_INT_H

#ifdef HAVE_HTTP3

#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/ssl.h>

#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "../hls/hls.h"
#include "../ts/pidfilter.h"
#include "../ts/ts_push.h"
#include "../dash/lldash.h"
#include "../segment/mp4push.h"
#include "../reactor/internal.h"

#define H3_MAX_REQS 16
/* TS-push holds its slot for the stream's life, unlike HLS/DASH GETs.
   reserved so it can't starve WS out of every slot on this connection */
#define H3_WS_RESERVE 2
#define H3_SCID_LEN 16
#define H3_PKT_MAX 1452
#define H3_IDLE_NS (30 * 1000000000ULL)
#define H3_PATH_MAX 8192
#define H3_MAX_CONNS_PER_THREAD 256

typedef struct h3_req {
  int active;
  int64_t stream_id;
  char method[16];
  char path[H3_PATH_MAX];
  char inm[80];           /* if-none-match header value, empty if absent */
  char origin[128];       /* origin header value, empty if absent */
  char protocol[16];      /* :protocol pseudo-header, RFC9220 extended CONNECT */
  char authz[200];        /* authorization header value, empty if absent */
  int dispatch_pending;
  int dispatched;         /* blocks second dispatch_req() call for this stream */
  uint8_t *resp_data;
  size_t resp_len;
  size_t resp_off;
  int resp_zc;
  int tspush_sub_idx;     /* -1 normal req, >=0 TS push subscriber slot */
  int dashchunk_sub_idx;  /* -1 normal req, >=0 dash/lldash.c subscriber slot */
  int mp4push_sub_idx;    /* -1 normal req, >=0 segment/mp4push.c subscriber slot */
  int ws_active;
  void *ws_parser;        /* ws_parser_t*, void* to keep ws_frame.h out of this header */
  uint8_t *ws_pending;    /* not yet handed to nghttp3 */
  size_t ws_pending_len, ws_pending_cap;
  int ws_inflight;
  uint8_t *ws_send_data; /* currently being read by nghttp3, zero-copy */
  size_t ws_send_len, ws_send_off;
  uint8_t *ws_prev_data; /* last fully-sent buffer, kept one cycle vs in-flight retransmit */
  pthread_mutex_t ws_lock; /* zero-init valid: default glibc mutex */
} h3_req_t;

typedef struct h3_conn {
  ngtcp2_conn *qconn;
  nghttp3_conn *h3conn;
  SSL *ssl;
  ngtcp2_crypto_ossl_ctx *ossl_ctx;
  ngtcp2_crypto_conn_ref conn_ref; /* must outlive ssl */
  struct sockaddr_storage peer_addr;
  socklen_t peer_addrlen;
  struct sockaddr_storage local_addr;
  socklen_t local_addrlen;
  ngtcp2_cid scid;
  uint8_t scid_data[H3_SCID_LEN];
  ngtcp2_cid odcid; /* client's original DCID */
  uint8_t odcid_data[NGTCP2_MAX_CIDLEN];
  int64_t h3_ctrl;
  int64_t h3_qenc;
  int64_t h3_qdec;
  h3_req_t reqs[H3_MAX_REQS];
  int handshake_done;
  int done;
  int active_idx; /* position in t_h3_active[], for O(1) swap-remove */
  int pool_slot;
  ngtcp2_tstamp last_rx;
} h3_conn_t;

/* t_h3_active[]: dense, no holes, iterate for "all connections" (h3_tick,
   idle sweeps, ts_push_h3_flush). t_h3_hash[]: open-addressing lookup by raw CID bytes, keyed SCID+ODCID. */
extern _Thread_local h3_conn_t **t_h3_active;
extern _Thread_local int t_h3_active_cnt;
extern _Thread_local h3_conn_t **t_h3_hash;
extern _Thread_local int t_h3_init;

static inline ngtcp2_tstamp h3_ts(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ngtcp2_tstamp)ts.tv_sec * 1000000000ULL + (ngtcp2_tstamp)ts.tv_nsec;
}

static inline h3_req_t *find_req(h3_conn_t *c, int64_t sid) {
  for (int i = 0; i < H3_MAX_REQS; i++) if (c->reqs[i].active && c->reqs[i].stream_id == sid) return &c->reqs[i];
  return NULL;
}

static inline h3_req_t *alloc_req(h3_conn_t *c, int64_t sid) {
  for (int i = 0; i < H3_MAX_REQS; i++) {
    if (!c->reqs[i].active) {
      memset(&c->reqs[i], 0, sizeof(h3_req_t));
      c->reqs[i].active = 1;
      c->reqs[i].stream_id = sid;
      c->reqs[i].tspush_sub_idx = -1;
      c->reqs[i].dashchunk_sub_idx = -1;
      c->reqs[i].mp4push_sub_idx = -1;
      return &c->reqs[i];
    }
  }
  return NULL;
}

static inline void free_req(h3_conn_t *c, int64_t sid) {
  for (int i = 0; i < H3_MAX_REQS; i++) {
    if (c->reqs[i].active && c->reqs[i].stream_id == sid) {
      hls_resp_body_release(c->reqs[i].resp_data, c->reqs[i].resp_zc);
      c->reqs[i].resp_data = NULL;
      if (c->reqs[i].tspush_sub_idx >= 0) {
        ts_push_unsubscribe_by_idx(c->reqs[i].tspush_sub_idx);
        c->reqs[i].tspush_sub_idx = -1;
      }
      if (c->reqs[i].dashchunk_sub_idx >= 0) {
        dash_lldash_sub_close(c->reqs[i].dashchunk_sub_idx);
        c->reqs[i].dashchunk_sub_idx = -1;
      }
      if (c->reqs[i].mp4push_sub_idx >= 0) {
        mp4push_sub_close(c->reqs[i].mp4push_sub_idx);
        c->reqs[i].mp4push_sub_idx = -1;
      }
      free(c->reqs[i].ws_pending);
      free(c->reqs[i].ws_send_data);
      free(c->reqs[i].ws_prev_data);
      c->reqs[i].active = 0;
      return;
    }
  }
}

/* from http3_quic.c */
h3_conn_t *h3conn_new(const uint8_t *pkt, size_t pktlen, const struct sockaddr *peer, socklen_t peerlen, const struct sockaddr *local, socklen_t locallen);
void h3conn_del(h3_conn_t *c);
h3_conn_t *find_conn(const uint8_t *pkt, size_t pktlen);
void flush_tx(h3_conn_t *c, int udp_fd);
int h3_tables_alloc(void); /* 1 = ready (already inited or just alloc'd), 0 = alloc failure */

/* from http3_req.c */
void dispatch_req(h3_conn_t *c, h3_req_t *r);
int cb_handshake_completed(ngtcp2_conn *qconn, void *ud);

/* from http3_resp.c */
void h3_respond_status(h3_conn_t *c, int64_t sid, const char *status);
void h3_respond_401(h3_conn_t *c, int64_t sid);
void h3_submit_resp(h3_conn_t *c, h3_req_t *r, int status, const char *content_type, const char *etag, size_t content_length, uint8_t *body, int zc, const char *origin_hdr);
void h3_respond_hls(h3_conn_t *c, h3_req_t *r, int handled, const hls_resp_t *resp);

/* from http3_tspush.c */
nghttp3_ssize h3_tspush_read_cb(nghttp3_conn *h3, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_ud, void *stream_ud);

/* from http3_dashchunk.c */
nghttp3_ssize h3_dashchunk_read_cb(nghttp3_conn *h3, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_ud, void *stream_ud);

/* from http3_mp4push.c */
nghttp3_ssize h3_mp4push_read_cb(nghttp3_conn *h3, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_ud, void *stream_ud);

/* from http3_llhls.c */
int h3_llhls_try_park(h3_conn_t *conn, int64_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, const char *inm, const char *origin_hdr,
                       uint32_t want_seg, int want_part, int timeout_ms, int ws_handle);
void h3_llhls_flush_waiters(void);
void h3_llhls_on_stream_close(h3_conn_t *c, int64_t stream_id);
void h3_llhls_on_conn_close(h3_conn_t *c);

/* from http3_hls_cold.c */
int h3_hls_cold_try_park(h3_conn_t *conn, int64_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid,
                          const char *filename, hls_cold_kind_t kind, seg_container_t container, int want_ll, int is_head,
                          const char *origin_hdr, int timeout_ms, int ws_handle);
void h3_hls_cold_flush_waiters(void);
void h3_hls_cold_on_stream_close(const h3_conn_t *c, int64_t stream_id);
void h3_hls_cold_on_conn_close(const h3_conn_t *c);

/* from http3_ws.c */
void h3_ws_dispatch(h3_conn_t *c, h3_req_t *r);
void h3_ws_flush(void);
void h3_ws_on_stream_close(h3_conn_t *c, int64_t stream_id);
void h3_ws_on_conn_close(h3_conn_t *c);
void h3_ws_data_chunk(h3_req_t *r, const uint8_t *data, size_t len);

#endif /* HAVE_HTTP3 */
#endif /* DIPIXY_HTTP3_INT_H */
