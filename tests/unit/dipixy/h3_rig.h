/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TEST_H3_RIG_H
#define DIPIXY_TEST_H3_RIG_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "../tls_fixture.h"
#include "client_resp.h"
#include "dipixy/http3/http3.h"
#include "dipixy/http3/http3_int.h"
#include "dipixy/reactor/internal.h"

#define H3R_STREAMS 6
#define H3R_PKT 1452
#define H3R_PUMP_MS 4000
#define H3R_VEC 16
#define H3R_RAW_STREAMS 4
#define H3R_RAW_BUF 512

typedef struct {
  char dir[64];
  char cert[96];
  char key[96];
  int srv_fd;
  struct sockaddr_in srv_addr;
  int cli_fd;
  struct sockaddr_in cli_addr;
  config_t cfg;
  SSL_CTX *sctx;
  SSL *ssl;
  ngtcp2_crypto_conn_ref ref;
  ngtcp2_crypto_ossl_ctx *octx;
  ngtcp2_conn *qc;
  nghttp3_conn *h3;
  int handshake_done;
  int closed_by_peer;
  client_resp_t resp[H3R_STREAMS];
  int64_t raw_sid[H3R_RAW_STREAMS];
  uint8_t raw_buf[H3R_RAW_STREAMS][H3R_RAW_BUF];
  size_t raw_len[H3R_RAW_STREAMS];
  int raw_n;
  const unsigned char *alpn;
  unsigned alpn_len;
  int no_alpn;
  uint8_t upload[2048];
  size_t upload_len;
  size_t upload_off;
} h3rig_t;

static inline int h3r_raw_index(const h3rig_t *h, int64_t sid) {
  for (int i = 0; i < h->raw_n; i++) {
    if (h->raw_sid[i] == sid) return i;
  }
  return -1;
}

static inline client_resp_t *h3r_resp_for(h3rig_t *h, int64_t sid) {
  for (int i = 0; i < H3R_STREAMS; i++) {
    if (h->resp[i].sid == sid && (h->resp[i].sid || i == 0)) return &h->resp[i];
  }
  for (int i = 0; i < H3R_STREAMS; i++) {
    if (!h->resp[i].sid) {
      h->resp[i].sid = sid;
      return &h->resp[i];
    }
  }
  return NULL;
}

static inline unsigned h3r_free_udp_port(void) {
  struct sockaddr_in a = {0};
  socklen_t alen = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &alen), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static inline ngtcp2_conn *h3r_get_conn(ngtcp2_crypto_conn_ref *ref) {
  return ((h3rig_t *)ref->user_data)->qc;
}

static inline int h3r_recv_header(nghttp3_conn *c, int64_t sid, int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags, void *ud, void *sud) {
  h3rig_t *h = ud;
  client_resp_t *st = h3r_resp_for(h, sid);
  nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
  nghttp3_vec v = nghttp3_rcbuf_get_buf(value);

  (void)c;
  (void)token;
  (void)flags;
  (void)sud;
  if (st) client_resp_header(st, n.base, n.len, v.base, v.len);
  return 0;
}

static inline int h3r_recv_data(nghttp3_conn *c, int64_t sid, const uint8_t *data, size_t len, void *ud, void *sud) {
  h3rig_t *h = ud;
  client_resp_t *st = h3r_resp_for(h, sid);

  (void)c;
  (void)sud;
  if (st) client_resp_data(st, data, len);
  return 0;
}

static inline int h3r_end_stream(nghttp3_conn *c, int64_t sid, void *ud, void *sud) {
  h3rig_t *h = ud;
  client_resp_t *st = h3r_resp_for(h, sid);

  (void)c;
  (void)sud;
  if (st) st->ended = 1;
  return 0;
}

static inline int h3r_h3_stream_close(nghttp3_conn *c, int64_t sid, uint64_t code, void *ud, void *sud) {
  h3rig_t *h = ud;
  client_resp_t *st = h3r_resp_for(h, sid);

  (void)c;
  (void)sud;
  if (st) {
    st->closed = 1;
    st->close_code = code;
  }
  return 0;
}

static inline int h3r_deferred_consume(nghttp3_conn *c, int64_t sid, size_t n, void *ud, void *sud) {
  h3rig_t *h = ud;

  (void)c;
  (void)sud;
  ngtcp2_conn_extend_max_stream_offset(h->qc, sid, n);
  ngtcp2_conn_extend_max_offset(h->qc, n);
  return 0;
}

static inline void h3r_setup_h3(h3rig_t *h) {
  nghttp3_callbacks cbs = {0};
  nghttp3_settings st;
  int64_t ctrl;
  int64_t qenc;
  int64_t qdec;

  cbs.recv_header = h3r_recv_header;
  cbs.recv_data = h3r_recv_data;
  cbs.end_stream = h3r_end_stream;
  cbs.stream_close = h3r_h3_stream_close;
  cbs.deferred_consume = h3r_deferred_consume;
  nghttp3_settings_default(&st);
  ck_assert_int_eq(nghttp3_conn_client_new(&h->h3, &cbs, &st, nghttp3_mem_default(), h), 0);
  ck_assert_int_eq(ngtcp2_conn_open_uni_stream(h->qc, &ctrl, NULL), 0);
  ck_assert_int_eq(ngtcp2_conn_open_uni_stream(h->qc, &qenc, NULL), 0);
  ck_assert_int_eq(ngtcp2_conn_open_uni_stream(h->qc, &qdec, NULL), 0);
  ck_assert_int_eq(nghttp3_conn_bind_control_stream(h->h3, ctrl), 0);
  ck_assert_int_eq(nghttp3_conn_bind_qpack_streams(h->h3, qenc, qdec), 0);
}

static inline int h3r_handshake_completed(ngtcp2_conn *c, void *ud) {
  h3rig_t *h = ud;

  (void)c;
  h->handshake_done = 1;
  h3r_setup_h3(h);
  return 0;
}

static inline int h3r_recv_stream_data(ngtcp2_conn *c, uint32_t flags, int64_t sid, uint64_t off, const uint8_t *data, size_t len, void *ud, void *sud) {
  h3rig_t *h = ud;
  nghttp3_ssize n;

  (void)c;
  (void)off;
  (void)sud;
  if (!h->h3) return 0;
  {
    int raw = h3r_raw_index(h, sid);

    if (raw >= 0) {
      size_t room = H3R_RAW_BUF - h->raw_len[raw];
      size_t take = len < room ? len : room;

      memcpy(h->raw_buf[raw] + h->raw_len[raw], data, take);
      h->raw_len[raw] += take;
      ngtcp2_conn_extend_max_stream_offset(h->qc, sid, len);
      ngtcp2_conn_extend_max_offset(h->qc, len);
      return 0;
    }
  }
  n = nghttp3_conn_read_stream2(h->h3, sid, data, len, (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0, h3_ts());
  if (n < 0) return NGTCP2_ERR_CALLBACK_FAILURE;
  ngtcp2_conn_extend_max_stream_offset(h->qc, sid, (uint64_t)n);
  ngtcp2_conn_extend_max_offset(h->qc, (uint64_t)n);
  return 0;
}

static inline int h3r_acked(ngtcp2_conn *c, int64_t sid, uint64_t off, uint64_t len, void *ud, void *sud) {
  h3rig_t *h = ud;

  (void)c;
  (void)off;
  (void)sud;
  if (h->h3 && nghttp3_conn_add_ack_offset(h->h3, sid, len) != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
  return 0;
}

static inline int h3r_q_stream_close(ngtcp2_conn *c, uint32_t flags, int64_t sid, uint64_t code, void *ud, void *sud) {
  h3rig_t *h = ud;

  (void)c;
  (void)sud;
  if (!(flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)) code = 0;
  if (h->h3) nghttp3_conn_close_stream(h->h3, sid, code);
  return 0;
}

static inline int h3r_extend_max_stream_data(ngtcp2_conn *c, int64_t sid, uint64_t max, void *ud, void *sud) {
  h3rig_t *h = ud;

  (void)c;
  (void)max;
  (void)sud;
  if (h->h3) nghttp3_conn_unblock_stream(h->h3, sid);
  return 0;
}

static inline void h3r_rand(uint8_t *dest, size_t len, const ngtcp2_rand_ctx *ctx) {
  (void)ctx;
  ck_assert_int_eq(RAND_bytes(dest, (int)len), 1);
}

static inline int h3r_new_cid(ngtcp2_conn *c, ngtcp2_cid *cid, ngtcp2_stateless_reset_token *token, size_t len, void *ud) {
  (void)c;
  (void)ud;
  ck_assert_int_eq(RAND_bytes(cid->data, (int)len), 1);
  cid->datalen = len;
  ck_assert_int_eq(RAND_bytes(token->data, (int)sizeof token->data), 1);
  return 0;
}

static inline void h3r_sockaddr(struct sockaddr_in *a, unsigned port) {
  memset(a, 0, sizeof *a);
  a->sin_family = AF_INET;
  a->sin_port = htons((uint16_t)port);
  a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static inline void h3r_server_start(h3rig_t *h) {
  unsigned port = h3r_free_udp_port();

  snprintf(h->dir, sizeof h->dir, "/tmp/dipixy_h3_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(h->dir));
  snprintf(h->cert, sizeof h->cert, "%s/c.crt", h->dir);
  snprintf(h->key, sizeof h->key, "%s/c.key", h->dir);
  tls_fixture_write_cert(h->cert, h->key);
  h3_init(h->cert, h->key);
  ck_assert_int_eq(h3_ready(), 1);
  h3_stateless_set_retry(H3_RETRY_OFF);
  h3_set_limits(0, 8, 0);
  h3_set_transport(0, 0, 0);
  h3_set_max_conns_per_thread(8);
  reactor_set_context(&h->cfg, NULL, NULL);
  h->srv_fd = h3_create_udp_sock((int)port, "127.0.0.1");
  ck_assert_int_ge(h->srv_fd, 0);
  t_h3_udp4 = h->srv_fd;
  h3r_sockaddr(&h->srv_addr, port);
}

static inline void h3r_client_start(h3rig_t *h) {
  ngtcp2_callbacks cbs = {0};
  ngtcp2_settings settings;
  ngtcp2_transport_params tp;
  ngtcp2_cid dcid;
  ngtcp2_cid scid;
  ngtcp2_path path;
  struct sockaddr_in local;
  socklen_t llen = sizeof local;
  uint8_t dcid_data[NGTCP2_MIN_INITIAL_DCIDLEN];
  uint8_t scid_data[NGTCP2_MIN_INITIAL_DCIDLEN];

  h->cli_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  ck_assert_int_ge(h->cli_fd, 0);
  h3r_sockaddr(&local, 0);
  ck_assert_int_eq(bind(h->cli_fd, (struct sockaddr *)&local, sizeof local), 0);
  ck_assert_int_eq(getsockname(h->cli_fd, (struct sockaddr *)&h->cli_addr, &llen), 0);

  h->sctx = SSL_CTX_new(TLS_client_method());
  ck_assert_ptr_nonnull(h->sctx);
  SSL_CTX_set_min_proto_version(h->sctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(h->sctx, TLS1_3_VERSION);
  SSL_CTX_set_verify(h->sctx, SSL_VERIFY_NONE, NULL);
  h->ssl = SSL_new(h->sctx);
  ck_assert_ptr_nonnull(h->ssl);
  ck_assert_int_eq(ngtcp2_crypto_ossl_configure_client_session(h->ssl), 0);
  h->ref.get_conn = h3r_get_conn;
  h->ref.user_data = h;
  SSL_set_app_data(h->ssl, &h->ref);
  SSL_set_connect_state(h->ssl);
  if (h->alpn_len) ck_assert_int_eq(SSL_set_alpn_protos(h->ssl, h->alpn, h->alpn_len), 0);
  else if (!h->no_alpn) ck_assert_int_eq(SSL_set_alpn_protos(h->ssl, (const unsigned char *)"\x02h3", 3), 0);
  ck_assert_int_eq(SSL_set_tlsext_host_name(h->ssl, "localhost"), 1);

  ck_assert_int_eq(RAND_bytes(dcid_data, sizeof dcid_data), 1);
  ck_assert_int_eq(RAND_bytes(scid_data, sizeof scid_data), 1);
  ngtcp2_cid_init(&dcid, dcid_data, sizeof dcid_data);
  ngtcp2_cid_init(&scid, scid_data, sizeof scid_data);

  cbs.client_initial = ngtcp2_crypto_client_initial_cb;
  cbs.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
  cbs.encrypt = ngtcp2_crypto_encrypt_cb;
  cbs.decrypt = ngtcp2_crypto_decrypt_cb;
  cbs.hp_mask = ngtcp2_crypto_hp_mask_cb;
  cbs.recv_retry = ngtcp2_crypto_recv_retry_cb;
  cbs.update_key = ngtcp2_crypto_update_key_cb;
  cbs.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
  cbs.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
  cbs.get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb;
  cbs.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
  cbs.handshake_completed = h3r_handshake_completed;
  cbs.recv_stream_data = h3r_recv_stream_data;
  cbs.acked_stream_data_offset = h3r_acked;
  cbs.stream_close = h3r_q_stream_close;
  cbs.extend_max_stream_data = h3r_extend_max_stream_data;
  cbs.rand = h3r_rand;
  cbs.get_new_connection_id2 = h3r_new_cid;

  ngtcp2_settings_default(&settings);
  settings.initial_ts = h3_ts();
  ngtcp2_transport_params_default(&tp);
  tp.initial_max_streams_uni = 3;
  tp.initial_max_streams_bidi = 16;
  tp.initial_max_data = 1u << 20;
  tp.initial_max_stream_data_bidi_local = 1u << 18;
  tp.initial_max_stream_data_bidi_remote = 1u << 18;
  tp.initial_max_stream_data_uni = 1u << 18;

  memset(&path, 0, sizeof path);
  path.local.addr = (ngtcp2_sockaddr *)&h->cli_addr;
  path.local.addrlen = sizeof h->cli_addr;
  path.remote.addr = (ngtcp2_sockaddr *)&h->srv_addr;
  path.remote.addrlen = sizeof h->srv_addr;
  ck_assert_int_eq(ngtcp2_conn_client_new(&h->qc, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1, &cbs, &settings, &tp, NULL, h), 0);
  ck_assert_int_eq(ngtcp2_crypto_ossl_ctx_new(&h->octx, h->ssl), 0);
  ngtcp2_conn_set_tls_native_handle(h->qc, h->octx);
}

static inline void h3r_client_flush(h3rig_t *h) {
  ngtcp2_path_storage ps;
  uint8_t buf[H3R_PKT];

  ngtcp2_path_storage_zero(&ps);
  for (;;) {
    int64_t sid = -1;
    int fin = 0;
    nghttp3_vec vec[H3R_VEC];
    ngtcp2_vec dv[H3R_VEC];
    nghttp3_ssize vcnt = 0;
    ngtcp2_ssize ndatalen = 0;
    ngtcp2_ssize nwrite;
    uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_NONE;

    if (h->h3) {
      vcnt = nghttp3_conn_writev_stream(h->h3, &sid, &fin, vec, H3R_VEC);
      ck_assert_int_ge((int)vcnt, 0);
      for (nghttp3_ssize i = 0; i < vcnt; i++) {
        dv[i].base = vec[i].base;
        dv[i].len = vec[i].len;
      }
      if (fin) flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
    }
    nwrite = ngtcp2_conn_writev_stream(h->qc, &ps.path, NULL, buf, sizeof buf, &ndatalen, flags, sid, dv, (size_t)vcnt, h3_ts());
    if (nwrite < 0) {
      if (nwrite == NGTCP2_ERR_STREAM_DATA_BLOCKED) {
        nghttp3_conn_block_stream(h->h3, sid);
        continue;
      }
      if (nwrite == NGTCP2_ERR_STREAM_SHUT_WR) {
        nghttp3_conn_shutdown_stream_write(h->h3, sid);
        continue;
      }
      if (nwrite == NGTCP2_ERR_WRITE_MORE) {
        nghttp3_conn_add_write_offset(h->h3, sid, (size_t)ndatalen);
        continue;
      }
      h->closed_by_peer = 1;
      return;
    }
    if (ndatalen >= 0 && sid >= 0) ck_assert_int_eq(nghttp3_conn_add_write_offset(h->h3, sid, (size_t)ndatalen), 0);
    if (nwrite == 0) break;
    ck_assert_int_eq((int)sendto(h->cli_fd, buf, (size_t)nwrite, 0, (struct sockaddr *)&h->srv_addr, sizeof h->srv_addr), (int)nwrite);
  }
}

static inline void h3r_client_recv(h3rig_t *h) {
  uint8_t buf[65536];
  ssize_t n;

  while ((n = recv(h->cli_fd, buf, sizeof buf, 0)) > 0) {
    ngtcp2_path path;
    ngtcp2_pkt_info pi = {0};
    int rv;

    memset(&path, 0, sizeof path);
    path.local.addr = (ngtcp2_sockaddr *)&h->cli_addr;
    path.local.addrlen = sizeof h->cli_addr;
    path.remote.addr = (ngtcp2_sockaddr *)&h->srv_addr;
    path.remote.addrlen = sizeof h->srv_addr;
    rv = ngtcp2_conn_read_pkt(h->qc, &path, &pi, buf, (size_t)n, h3_ts());
    if (rv != 0) {
      h->closed_by_peer = 1;
      return;
    }
  }
  if (ngtcp2_conn_get_expiry(h->qc) <= h3_ts()) {
    if (ngtcp2_conn_handle_expiry(h->qc, h3_ts()) != 0) h->closed_by_peer = 1;
  }
}

static inline void h3r_server_step(const h3rig_t *h) {
  struct pollfd pfd = {.fd = h->srv_fd, .events = POLLIN};

  if (poll(&pfd, 1, 2) > 0) h3_handle_readable(h->srv_fd);
  h3_tick();
}

static inline int h3r_cond_met(int (*cond)(h3rig_t *, void *), h3rig_t *h, void *arg) {
  return cond ? cond(h, arg) : 0;
}

static inline int h3r_pump_timed(h3rig_t *h, int (*cond)(h3rig_t *, void *), void *arg, unsigned ms) {
  uint64_t deadline = h3_ts() + (uint64_t)ms * 1000000ULL;

  while (h3_ts() < deadline) {
    struct pollfd pfd = {.fd = h->srv_fd, .events = POLLIN};

    if (h3r_cond_met(cond, h, arg)) return 1;
    if (!h->closed_by_peer) h3r_client_flush(h);
    if (poll(&pfd, 1, 2) > 0) h3_handle_readable(h->srv_fd);
    h3_tick();
    h3_ws_flush();
    h3r_client_recv(h);
    if (h->closed_by_peer) return h3r_cond_met(cond, h, arg);
  }
  return h3r_cond_met(cond, h, arg);
}

static inline int h3r_pump_until(h3rig_t *h, int (*cond)(h3rig_t *, void *), void *arg) {
  return h3r_pump_timed(h, cond, arg, H3R_PUMP_MS);
}

static inline int h3r_cond_handshake(h3rig_t *h, void *arg) {
  (void)arg;
  return h->handshake_done;
}

static inline int h3r_cond_server_ready(h3rig_t *h, void *arg) {
  (void)h;
  (void)arg;
  return t_h3_init && t_h3_active_cnt == 1 && t_h3_active[0]->h3conn != NULL;
}

static inline int h3r_cond_server_ready_multi(h3rig_t *h, void *arg) {
  (void)h;
  (void)arg;
  return t_h3_init && t_h3_active_cnt > 0 && t_h3_active[t_h3_active_cnt - 1]->h3conn != NULL;
}

static inline int h3r_cond_ended(h3rig_t *h, void *arg) {
  const client_resp_t *r = h3r_resp_for(h, *(int64_t *)arg);

  return r && (r->ended || r->closed);
}

static inline void h3r_open(h3rig_t *h) {
  memset(h, 0, sizeof *h);
  h->srv_fd = -1;
  h->cli_fd = -1;
  h3r_server_start(h);
  h3r_client_start(h);
  ck_assert_int_eq(h3r_pump_until(h, h3r_cond_handshake, NULL), 1);
  ck_assert_int_eq(h3r_pump_until(h, h3r_cond_server_ready, NULL), 1);
}

static inline void h3r_client_free(h3rig_t *h) {
  if (h->h3) nghttp3_conn_del(h->h3);
  if (h->qc) ngtcp2_conn_del(h->qc);
  if (h->octx) ngtcp2_crypto_ossl_ctx_del(h->octx);
  if (h->ssl) {
    SSL_set_app_data(h->ssl, NULL);
    SSL_free(h->ssl);
  }
  if (h->sctx) SSL_CTX_free(h->sctx);
  if (h->cli_fd >= 0) close(h->cli_fd);
  h->h3 = NULL;
  h->qc = NULL;
  h->octx = NULL;
  h->ssl = NULL;
  h->sctx = NULL;
  h->cli_fd = -1;
}

static inline void h3r_server_stop(h3rig_t *h) {
  char cmd[160];

  h3_thread_cleanup();
  if (h->srv_fd >= 0) close(h->srv_fd);
  h->srv_fd = -1;
  t_h3_udp4 = -1;
  h3_cleanup();
  snprintf(cmd, sizeof cmd, "rm -rf %s", h->dir);
  ck_assert_int_eq(system(cmd), 0);
}

static inline void h3r_close(h3rig_t *h) {
  h3r_client_free(h);
  h3r_server_stop(h);
}

static inline size_t h3r_initial_packet(h3rig_t *h, uint8_t *out, size_t cap) {
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  int cap_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  ssize_t n;

  ck_assert_int_ge(cap_fd, 0);
  h3r_sockaddr(&a, 0);
  ck_assert_int_eq(bind(cap_fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(cap_fd, (struct sockaddr *)&h->srv_addr, &alen), 0);
  h3r_client_start(h);
  h3r_client_flush(h);
  n = recv(cap_fd, out, cap, 0);
  ck_assert_int_gt((int)n, 0);
  close(cap_fd);
  return (size_t)n;
}

static inline void h3r_send_raw(h3rig_t *h, int64_t sid, const uint8_t *data, size_t len, int fin) {
  ngtcp2_path_storage ps;
  uint8_t buf[H3R_PKT];
  size_t off = 0;

  ngtcp2_path_storage_zero(&ps);
  if (h3r_raw_index(h, sid) < 0 && h->raw_n < H3R_RAW_STREAMS) h->raw_sid[h->raw_n++] = sid;
  while (off < len || fin) {
    ngtcp2_vec vec = {(uint8_t *)data + off, len - off};
    ngtcp2_ssize ndatalen = 0;
    ngtcp2_ssize nwrite = ngtcp2_conn_writev_stream(h->qc, &ps.path, NULL, buf, sizeof buf, &ndatalen, fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : NGTCP2_WRITE_STREAM_FLAG_NONE, sid, &vec, len - off ? 1 : 0, h3_ts());

    ck_assert_int_ge((int)nwrite, 0);
    if (ndatalen > 0) off += (size_t)ndatalen;
    if (nwrite > 0) ck_assert_int_eq((int)sendto(h->cli_fd, buf, (size_t)nwrite, 0, (struct sockaddr *)&h->srv_addr, sizeof h->srv_addr), (int)nwrite);
    if (off >= len && (!fin || nwrite > 0)) break;
    if (nwrite == 0) break;
  }
}

static inline nghttp3_ssize h3r_upload_read(nghttp3_conn *c, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *flags, void *ud, void *sud) {
  h3rig_t *h = ud;
  size_t n = h->upload_len - h->upload_off;

  (void)c;
  (void)sid;
  (void)veccnt;
  (void)sud;
  if (!n) return NGHTTP3_ERR_WOULDBLOCK;
  vec[0].base = h->upload + h->upload_off;
  vec[0].len = n;
  h->upload_off += n;
  *flags = NGHTTP3_DATA_FLAG_NONE;
  return 1;
}

static inline int64_t h3r_connect(h3rig_t *h, const char *path, const char *protocol) {
  nghttp3_nv nva[] = {
      {(uint8_t *)":method", (uint8_t *)"CONNECT", 7, 7, NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)":authority", (uint8_t *)"localhost", 10, 9, NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)":protocol", (uint8_t *)protocol, 9, strlen(protocol), NGHTTP3_NV_FLAG_NONE},
  };
  nghttp3_data_reader dr;
  int64_t sid;

  dr.read_data = h3r_upload_read;
  ck_assert_int_eq(ngtcp2_conn_open_bidi_stream(h->qc, &sid, NULL), 0);
  ck_assert_int_eq(nghttp3_conn_submit_request(h->h3, sid, nva, 5, &dr, NULL), 0);
  return sid;
}

static inline void h3r_upload(h3rig_t *h, int64_t sid, const uint8_t *data, size_t len) {
  ck_assert_uint_le(h->upload_len + len, sizeof h->upload);
  memcpy(h->upload + h->upload_len, data, len);
  h->upload_len += len;
  ck_assert_int_eq(nghttp3_conn_resume_stream(h->h3, sid), 0);
}

static inline int64_t h3r_request(h3rig_t *h, const char *method, const char *path, const nghttp3_nv *extra, size_t n_extra) {
  nghttp3_nv nva[12] = {
      {(uint8_t *)":method", (uint8_t *)method, 7, strlen(method), NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)":authority", (uint8_t *)"localhost", 10, 9, NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP3_NV_FLAG_NONE},
  };
  size_t n = 4;
  int64_t sid;

  for (size_t i = 0; i < n_extra; i++) nva[n++] = extra[i];
  ck_assert_int_eq(ngtcp2_conn_open_bidi_stream(h->qc, &sid, NULL), 0);
  ck_assert_int_eq(nghttp3_conn_submit_request(h->h3, sid, nva, n, NULL, NULL), 0);
  return sid;
}

static inline int h3r_raw_status(const h3rig_t *h, int64_t sid) {
  int raw = h3r_raw_index(h, sid);
  size_t pos = 0;

  if (raw < 0) return 0;
  while (pos + 2 < h->raw_len[raw]) {
    uint8_t type = h->raw_buf[raw][pos];
    uint8_t len = h->raw_buf[raw][pos + 1];
    if (type == 0x01 && pos + 2 + 3 <= h->raw_len[raw]) {
      uint8_t field = h->raw_buf[raw][pos + 4];
      switch (field) {
        case 0xD9: return 200;
        case 0xDA: return 304;
        case 0xDB: return 404;
        case 0xDC: return 503;
        default: return -1;
      }
    }
    pos += 2u + len;
  }
  return 0;
}

static inline int h3r_cond_raw_status(h3rig_t *h, void *arg) {
  return h3r_raw_status(h, *(int64_t *)arg) != 0;
}

static inline int h3r_wait_response(h3rig_t *h, int64_t sid) {
  return h3r_pump_until(h, h3r_cond_ended, &sid);
}

#endif
