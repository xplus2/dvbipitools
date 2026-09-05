/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* HTTP/3 QUIC transport: connection lifecycle, ngtcp2 callbacks, handshake,
   TX/RX loops, global TLS context */

#ifdef HAVE_HTTP3

#include "http3.h"
#include "http3_int.h"

#include "lib/helper/log.h"

#include <openssl/rand.h>

#include <arpa/inet.h>
#include <unistd.h>

static SSL_CTX *g_h3_ssl_ctx = NULL;

static int g_h3_max_conns = H3_MAX_CONNS_PER_THREAD;
static uint32_t g_h3_hash_cap = H3_MAX_CONNS_PER_THREAD * 4u;

void h3_set_max_conns_per_thread(int n) {
  if (n < 1) n = 1;
  if (n > H3_MAX_CONNS_PER_THREAD) n = H3_MAX_CONNS_PER_THREAD;
  g_h3_max_conns = n;
  g_h3_hash_cap = (uint32_t)next_pow2((size_t)n * 4);
  if (g_h3_hash_cap < 4) g_h3_hash_cap = 4;
}

_Thread_local h3_conn_t **t_h3_active = NULL;
_Thread_local int t_h3_active_cnt = 0;
_Thread_local h3_conn_t **t_h3_hash = NULL;
_Thread_local int t_h3_init = 0;
_Thread_local int t_h3_udp4 = -1;
_Thread_local int t_h3_udp6 = -1;

static _Thread_local h3_conn_t *t_h3_pool = NULL;
static _Thread_local int *t_h3_pool_free = NULL;
static _Thread_local int t_h3_pool_free_n = 0;
static _Thread_local uint32_t t_h3_hash_cap = 0;

/* deleted marker: unlike NULL, doesn't stop a probe */
#define H3_HASH_TOMB ((h3_conn_t *)(uintptr_t)1)

int h3_tables_alloc(void) {
  if (t_h3_init)
    return 1;
  t_h3_pool = calloc((size_t)g_h3_max_conns, sizeof *t_h3_pool);
  t_h3_pool_free = malloc(sizeof *t_h3_pool_free * (size_t)g_h3_max_conns);
  t_h3_active = malloc(sizeof *t_h3_active * (size_t)g_h3_max_conns);
  t_h3_hash = calloc((size_t)g_h3_hash_cap, sizeof *t_h3_hash);
  if (!t_h3_pool || !t_h3_pool_free || !t_h3_active || !t_h3_hash) {
    free(t_h3_pool);
    free(t_h3_pool_free);
    free(t_h3_active);
    free(t_h3_hash);
    t_h3_pool = NULL;
    t_h3_pool_free = NULL;
    t_h3_active = NULL;
    t_h3_hash = NULL;
    return 0;
  }
  for (int i = 0; i < g_h3_max_conns; i++)
    t_h3_pool_free[i] = i;
  t_h3_pool_free_n = g_h3_max_conns;
  t_h3_hash_cap = g_h3_hash_cap;
  t_h3_init = 1;
  return 1;
}

static uint32_t cid_hash(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++)
    h = (h ^ data[i]) * 16777619u;
  return h;
}

static void h3_hash_insert(const uint8_t *key, size_t keylen, h3_conn_t *c) {
  uint32_t i = cid_hash(key, keylen) & (t_h3_hash_cap - 1);
  for (uint32_t n = 0; n < t_h3_hash_cap; n++, i = (i + 1) & (t_h3_hash_cap - 1)) {
    if (t_h3_hash[i] == NULL || t_h3_hash[i] == H3_HASH_TOMB) {
      t_h3_hash[i] = c;
      return;
    }
  }
}

/* tombstones only (key,c)'s own slot, other conns' probe chains intact */
static void h3_hash_delete(const uint8_t *key, size_t keylen, const h3_conn_t *c) {
  uint32_t i = cid_hash(key, keylen) & (t_h3_hash_cap - 1);
  for (uint32_t n = 0; n < t_h3_hash_cap; n++, i = (i + 1) & (t_h3_hash_cap - 1)) {
    if (t_h3_hash[i] == NULL)
      return;
    if (t_h3_hash[i] == c) {
      t_h3_hash[i] = H3_HASH_TOMB;
      return;
    }
  }
}

static ngtcp2_conn *get_ngtcp2_conn(ngtcp2_crypto_conn_ref *ref) {
  return ((h3_conn_t *)ref->user_data)->qconn;
}

static void cb_rand(uint8_t *dest, size_t destlen, const ngtcp2_rand_ctx *ctx) {
  (void)ctx;
  RAND_bytes(dest, (int)destlen);
}

static int cb_get_new_connection_id2(ngtcp2_conn *qconn, ngtcp2_cid *cid, ngtcp2_stateless_reset_token *token, size_t cidlen, void *ud) {
  (void)qconn;
  (void)ud;
  RAND_bytes(cid->data, (int)cidlen);
  cid->datalen = cidlen;
  RAND_bytes(token->data, sizeof(token->data));
  return 0;
}

static int cb_recv_stream_data(ngtcp2_conn *qconn, uint32_t flags, int64_t stream_id, uint64_t offset, const uint8_t *data, size_t datalen, void *ud, void *stream_ud) {
  (void)offset;
  (void)stream_ud;
  h3_conn_t *c = ud;
  if (!c->h3conn)
    return 0;

  nghttp3_ssize consumed = nghttp3_conn_read_stream(c->h3conn, stream_id, data, datalen, (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0);
  if (consumed < 0)
    return NGTCP2_ERR_CALLBACK_FAILURE;

  ngtcp2_conn_extend_max_stream_offset(qconn, stream_id, (uint64_t)consumed);
  ngtcp2_conn_extend_max_offset(qconn, (uint64_t)consumed);
  return 0;
}

/* ngtcp2-level ack notice, forwards into nghttp3's own acked_stream_data */
static int cb_acked_stream_data_offset(ngtcp2_conn *qconn, int64_t stream_id, uint64_t offset, uint64_t datalen, void *ud, void *stream_ud) {
  (void)qconn;
  (void)offset;
  (void)stream_ud;
  h3_conn_t *c = ud;
  if (c->h3conn && nghttp3_conn_add_ack_offset(c->h3conn, stream_id, datalen) != 0)
    return NGTCP2_ERR_CALLBACK_FAILURE;
  return 0;
}

static int cb_stream_open(ngtcp2_conn *qconn, int64_t stream_id, void *ud) {
  (void)qconn;
  if ((stream_id & 0x02) == 0) /* bidi only, uni streams (control/qpack) need no req slot */
    alloc_req(ud, stream_id);
  return 0;
}

static int cb_stream_close(ngtcp2_conn *qconn, uint32_t flags, int64_t stream_id, uint64_t app_err, void *ud, void *stream_ud) {
  (void)qconn;
  (void)flags;
  (void)app_err;
  (void)stream_ud;
  h3_conn_t *c = ud;
  if (c->h3conn)
    nghttp3_conn_close_stream(c->h3conn, stream_id, app_err);
  h3_llhls_on_stream_close(c, stream_id);
  h3_hls_cold_on_stream_close(c, stream_id);
  h3_ws_on_stream_close(c, stream_id);
  free_req(c, stream_id);
  return 0;
}

void flush_tx(h3_conn_t *c, int udp_fd) {
  ngtcp2_tstamp ts = h3_ts();
  uint8_t pkt[H3_PKT_MAX];

  for (;;) {
    int64_t stream_id = -1;
    int fin = 0;
    ngtcp2_vec qvec[16];
    size_t qvcnt = 0;

    if (c->h3conn) {
      nghttp3_vec h3vec[16];
      nghttp3_ssize nread = nghttp3_conn_writev_stream(c->h3conn, &stream_id, &fin, h3vec, 16);
      if (nread > 0) {
        for (int i = 0; i < (int)nread; i++) {
          qvec[i].base = h3vec[i].base;
          qvec[i].len = h3vec[i].len;
        }
        qvcnt = (size_t)nread;
      }
    }

    ngtcp2_ssize ndatalen = 0;
    /* bare ngtcp2_path has uninit addr pointers, path_storage needed. output unused (we send to peer_addr) */
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);
    ngtcp2_pkt_info pi;
    /* qvcnt==0 with fin set: must still carry FIN and report via add_write_offset(...,0), or stream never terminates */
    uint32_t flags = fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : 0;
    ngtcp2_ssize pktlen = ngtcp2_conn_writev_stream_versioned(c->qconn, &ps.path, NGTCP2_PKT_INFO_VERSION, &pi, pkt, sizeof pkt, &ndatalen, flags, stream_id, qvcnt > 0 ? qvec : NULL, qvcnt, ts);
    if (pktlen == 0)
      break;
    if (pktlen < 0) {
      if (pktlen == NGTCP2_ERR_WRITE_MORE) {
        if (ndatalen >= 0 && c->h3conn)
          nghttp3_conn_add_write_offset(c->h3conn, stream_id, (uint64_t)ndatalen);
        continue;
      }
      c->done = 1;
      break;
    }
    sendto(udp_fd, pkt, (size_t)pktlen, 0, (const struct sockaddr *)&c->peer_addr, c->peer_addrlen);
    if (ndatalen >= 0 && c->h3conn)
      nghttp3_conn_add_write_offset(c->h3conn, stream_id, (uint64_t)ndatalen);
  }
}

static void h3_pool_release(const h3_conn_t *c) { t_h3_pool_free[t_h3_pool_free_n++] = c->pool_slot; }

h3_conn_t *h3conn_new(const uint8_t *pkt, size_t pktlen, const struct sockaddr *peer, socklen_t peerlen, const struct sockaddr *local, socklen_t locallen) {
  ngtcp2_pkt_hd hd;
  if (ngtcp2_accept(&hd, pkt, pktlen) != 0)
    return NULL;

  if (!h3_tables_alloc())
    return NULL;
  if (t_h3_active_cnt >= g_h3_max_conns)
    return NULL;

  if (t_h3_pool_free_n == 0)
    return NULL;
  int slot = t_h3_pool_free[--t_h3_pool_free_n];
  h3_conn_t *c = &t_h3_pool[slot];
  memset(c, 0, sizeof *c);
  c->pool_slot = slot;
  for (int i = 0; i < H3_MAX_REQS; i++) /* zeroed above: 0 is a valid tspush_sub_idx slot, not "none" */
    c->reqs[i].tspush_sub_idx = -1;

  RAND_bytes(c->scid_data, H3_SCID_LEN);
  ngtcp2_cid_init(&c->scid, c->scid_data, H3_SCID_LEN);
  /* save client's original DCID: retransmitted Initials still target it until client adopts our scid */
  memcpy(c->odcid_data, hd.dcid.data, hd.dcid.datalen);
  ngtcp2_cid_init(&c->odcid, c->odcid_data, hd.dcid.datalen);

  memcpy(&c->peer_addr, peer, peerlen);
  c->peer_addrlen = peerlen;
  memcpy(&c->local_addr, local, locallen);
  c->local_addrlen = locallen;

  c->ssl = SSL_new(g_h3_ssl_ctx);
  if (!c->ssl) {
    h3_pool_release(c);
    return NULL;
  }

  c->conn_ref.get_conn = get_ngtcp2_conn;
  c->conn_ref.user_data = c;
  SSL_set_app_data(c->ssl, &c->conn_ref);
  ngtcp2_crypto_ossl_configure_server_session(c->ssl);
  SSL_set_accept_state(c->ssl); /* mark as QUIC server (else "connection type not set") */

  ngtcp2_callbacks qcbs = {0};
  qcbs.recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
  qcbs.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
  qcbs.encrypt = ngtcp2_crypto_encrypt_cb;
  qcbs.decrypt = ngtcp2_crypto_decrypt_cb;
  qcbs.hp_mask = ngtcp2_crypto_hp_mask_cb;
  qcbs.update_key = ngtcp2_crypto_update_key_cb;
  qcbs.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
  qcbs.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
  qcbs.get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb;
  qcbs.handshake_completed = cb_handshake_completed;
  qcbs.recv_stream_data = cb_recv_stream_data;
  qcbs.acked_stream_data_offset = cb_acked_stream_data_offset;
  qcbs.stream_open = cb_stream_open;
  qcbs.stream_close = cb_stream_close;
  qcbs.rand = cb_rand;
  qcbs.get_new_connection_id2 = cb_get_new_connection_id2;

  ngtcp2_settings settings;
  ngtcp2_settings_default_versioned(NGTCP2_SETTINGS_VERSION, &settings);
  settings.initial_ts = h3_ts();

  ngtcp2_transport_params tp;
  ngtcp2_transport_params_default_versioned(NGTCP2_TRANSPORT_PARAMS_VERSION, &tp);
  tp.initial_max_streams_uni = 3;
  tp.initial_max_streams_bidi = H3_MAX_REQS;
  tp.initial_max_data = 1 * 1024 * 1024;
  tp.initial_max_stream_data_bidi_local = 256 * 1024;
  tp.initial_max_stream_data_bidi_remote = 256 * 1024;
  tp.initial_max_stream_data_uni = 256 * 1024;
  tp.max_idle_timeout = H3_IDLE_NS;
  tp.original_dcid = hd.dcid;
  tp.original_dcid_present = 1; /* required by ngtcp2 for servers */

  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_addr_init(&ps.path.local, local, locallen);
  ngtcp2_addr_init(&ps.path.remote, peer, peerlen);

  /* remote DCID = client's SCID (hd.scid). hd.dcid only "original DCID" for transport-param validation */
  if (ngtcp2_conn_server_new_versioned(&c->qconn, &hd.scid, &c->scid, &ps.path, hd.version, NGTCP2_CALLBACKS_VERSION, &qcbs, NGTCP2_SETTINGS_VERSION, &settings, NGTCP2_TRANSPORT_PARAMS_VERSION, &tp, NULL, c) != 0) {
    SSL_set_app_data(c->ssl, NULL);
    SSL_free(c->ssl);
    h3_pool_release(c);
    return NULL;
  }

  if (ngtcp2_crypto_ossl_ctx_new(&c->ossl_ctx, c->ssl) != 0) {
    ngtcp2_conn_del(c->qconn);
    SSL_set_app_data(c->ssl, NULL);
    SSL_free(c->ssl);
    h3_pool_release(c);
    return NULL;
  }
  ngtcp2_conn_set_tls_native_handle(c->qconn, c->ossl_ctx);
  c->h3_ctrl = c->h3_qenc = c->h3_qdec = -1;
  c->last_rx = h3_ts();
  c->active_idx = t_h3_active_cnt;
  t_h3_active[t_h3_active_cnt++] = c;
  h3_hash_insert(c->scid.data, c->scid.datalen, c);
  h3_hash_insert(c->odcid.data, c->odcid.datalen, c);
  return c;
}

void h3conn_del(h3_conn_t *c) {
  if (!c)
    return;
  h3_llhls_on_conn_close(c);
  h3_hls_cold_on_conn_close(c);
  h3_ws_on_conn_close(c);
  if (c->h3conn) {
    nghttp3_conn_del(c->h3conn);
    c->h3conn = NULL;
  }
  for (int i = 0; i < H3_MAX_REQS; i++) {
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
  }
  if (c->ossl_ctx) {
    ngtcp2_crypto_ossl_ctx_del(c->ossl_ctx);
    c->ossl_ctx = NULL;
  }
  if (c->qconn) {
    ngtcp2_conn_del(c->qconn);
    c->qconn = NULL;
  }
  if (c->ssl) {
    SSL_set_app_data(c->ssl, NULL);
    SSL_free(c->ssl);
    c->ssl = NULL;
  }

  h3_hash_delete(c->scid.data, c->scid.datalen, c);
  h3_hash_delete(c->odcid.data, c->odcid.datalen, c);

  int last = --t_h3_active_cnt;
  if (c->active_idx != last) {
    t_h3_active[c->active_idx] = t_h3_active[last];
    t_h3_active[c->active_idx]->active_idx = c->active_idx;
  }
  t_h3_active[last] = NULL;

  h3_pool_release(c);
}

h3_conn_t *find_conn(const uint8_t *pkt, size_t pktlen) {
  /* ngtcp2's untrusted first-pass parse (long/short hdrs, length checks, fixed DCID len = our SCID len).
     Malformed/version-negotiation packets return non-zero, routed as "new". */
  if (!t_h3_init)
    return NULL;

  ngtcp2_version_cid vc;
  if (ngtcp2_pkt_decode_version_cid(&vc, pkt, pktlen, H3_SCID_LEN) != 0)
    return NULL;

  uint32_t i = cid_hash(vc.dcid, vc.dcidlen) & (t_h3_hash_cap - 1);
  for (uint32_t n = 0; n < t_h3_hash_cap; n++, i = (i + 1) & (t_h3_hash_cap - 1)) {
    h3_conn_t *c = t_h3_hash[i];
    if (!c)
      return NULL;
    if (c == H3_HASH_TOMB || c->done)
      continue;
    if ((c->scid.datalen == vc.dcidlen && memcmp(c->scid.data, vc.dcid, vc.dcidlen) == 0) ||
        (c->odcid.datalen == vc.dcidlen && memcmp(c->odcid.data, vc.dcid, vc.dcidlen) == 0))
      return c;
  }
  return NULL;
}

static int h3_alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg) {
  (void)ssl;
  (void)arg;
  static const unsigned char alpn[] = "\x02h3";
  if (SSL_select_next_proto((unsigned char **)out, outlen, alpn, sizeof alpn - 1, in, inlen) == OPENSSL_NPN_NEGOTIATED)
    return SSL_TLSEXT_ERR_OK;
  return SSL_TLSEXT_ERR_NOACK;
}

void h3_init(const char *cert_path, const char *key_path) {
  ngtcp2_crypto_ossl_init();

  g_h3_ssl_ctx = SSL_CTX_new(TLS_server_method());
  if (!g_h3_ssl_ctx)
    return;

  SSL_CTX_set_min_proto_version(g_h3_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(g_h3_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_mode(g_h3_ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
  SSL_CTX_set_alpn_select_cb(g_h3_ssl_ctx, h3_alpn_select_cb, NULL);

  if (SSL_CTX_use_certificate_chain_file(g_h3_ssl_ctx, cert_path) != 1 || SSL_CTX_use_PrivateKey_file(g_h3_ssl_ctx, key_path, SSL_FILETYPE_PEM) != 1) {
    SSL_CTX_free(g_h3_ssl_ctx);
    g_h3_ssl_ctx = NULL;
    return;
  }
  log_line("http3: quic context ready");
}

void h3_cleanup(void) {
  if (g_h3_ssl_ctx) {
    SSL_CTX_free(g_h3_ssl_ctx);
    g_h3_ssl_ctx = NULL;
  }
}

/* caller (reactor.c) owns epoll registration, its dispatch loop keys off
   reactor_listener pointers not raw fds */
int h3_create_udp_sock(int port, const char *host) {
  if (!g_h3_ssl_ctx)
    return -1;

  int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (sock < 0)
    return -1;

  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt);
  /* set DF, no IP fragmentation: QUIC datagrams must drop not fragment, for path-MTU discovery (RFC 9000 14) */
  int mtud = IP_PMTUDISC_PROBE;
  setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &mtud, sizeof mtud);

  struct sockaddr_in saddr = {0};
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, host, &saddr.sin_addr);

  if (bind(sock, (struct sockaddr *)&saddr, sizeof saddr) < 0) {
    close(sock);
    return -1;
  }
  return sock;
}

int h3_create_udp_sock6(int port, const char *host6) {
  if (!g_h3_ssl_ctx)
    return -1;

  int sock = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (sock < 0)
    return -1;

  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt);
  setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof opt);
  /* set DF, no IP fragmentation (QUIC path-MTU discovery, RFC 9000 14) */
  int mtud6 = IPV6_PMTUDISC_PROBE;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &mtud6, sizeof mtud6);

  struct sockaddr_in6 saddr6 = {0};
  saddr6.sin6_family = AF_INET6;
  saddr6.sin6_port = htons((uint16_t)port);
  if (!host6 || !host6[0] || strcmp(host6, "::") == 0)
    saddr6.sin6_addr = in6addr_any;
  else
    inet_pton(AF_INET6, host6, &saddr6.sin6_addr);

  if (bind(sock, (struct sockaddr *)&saddr6, sizeof saddr6) < 0) {
    close(sock);
    return -1;
  }
  return sock;
}

#endif /* HAVE_HTTP3 */
