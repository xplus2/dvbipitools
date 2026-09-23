/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../http3.h"
#include "../http3_int.h"

#include <openssl/rand.h>
#include <string.h>

static ngtcp2_conn *get_ngtcp2_conn(ngtcp2_crypto_conn_ref *ref) {
  return ((h3_conn_t *)ref->user_data)->qconn;
}

static void h3_pool_release(const h3_conn_t *c) {
  free(c->reqs);
  t_h3_pool_free[t_h3_pool_free_n++] = c->pool_slot;
}

h3_conn_t *h3conn_new(const uint8_t *pkt, size_t pktlen, const struct sockaddr *peer, socklen_t peerlen, const struct sockaddr *local, socklen_t locallen, const h3_admit_t *ai) {
  ngtcp2_pkt_hd hd;
  if (ngtcp2_accept(&hd, pkt, pktlen) != 0) return NULL;
  if (!h3_tables_alloc()) return NULL;
  if (t_h3_active_cnt >= g_h3_max_conns) return NULL;
  if (t_h3_pool_free_n == 0) return NULL;
  int slot = t_h3_pool_free[--t_h3_pool_free_n];
  h3_conn_t *c = &t_h3_pool[slot];
  memset(c, 0, sizeof *c);
  c->pool_slot = slot;
  c->max_reqs = g_h3_max_reqs;
  c->reqs = calloc((size_t)c->max_reqs, sizeof *c->reqs);
  if (!c->reqs) {
    h3_pool_release(c);
    return NULL;
  }

  RAND_bytes(c->scid_data, H3_SCID_LEN);
  h3_steer_tag_cid(c->scid_data);
  ngtcp2_cid_init(&c->scid, c->scid_data, H3_SCID_LEN);
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
  SSL_set_accept_state(c->ssl);
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
  qcbs.remove_connection_id = cb_remove_connection_id;
  qcbs.path_validation = cb_path_validation;

  ngtcp2_settings settings;
  ngtcp2_settings_default_versioned(NGTCP2_SETTINGS_VERSION, &settings);
  settings.initial_ts = h3_ts();
  settings.cc_algo = g_h3_cc;
  settings.max_tx_udp_payload_size = g_h3_max_udp;
  if (g_h3_nprobes) {
    settings.pmtud_probes = g_h3_probes;
    settings.pmtud_probeslen = g_h3_nprobes;
  }
  if (ai && ai->tokenlen) {
    settings.token = ai->token;
    settings.tokenlen = ai->tokenlen;
    settings.token_type = ai->type;
  }

  ngtcp2_transport_params tp;
  ngtcp2_transport_params_default_versioned(NGTCP2_TRANSPORT_PARAMS_VERSION, &tp);
  tp.initial_max_streams_uni = 3;
  tp.initial_max_streams_bidi = (uint64_t)c->max_reqs;
  tp.initial_max_data = 4 * g_h3_window;
  tp.initial_max_stream_data_bidi_local = g_h3_window;
  tp.initial_max_stream_data_bidi_remote = g_h3_window;
  tp.initial_max_stream_data_uni = g_h3_window;
  tp.max_udp_payload_size = g_h3_max_udp;
  tp.max_idle_timeout = g_h3_idle_ns;
  tp.original_dcid_present = 1;
  if (ai && ai->retried) {
    tp.original_dcid = ai->odcid;
    tp.retry_scid = hd.dcid;
    tp.retry_scid_present = 1;
  } else {
    tp.original_dcid = hd.dcid;
  }
  if (h3_reset_token(tp.stateless_reset_token, &c->scid) != 0) {
    SSL_set_app_data(c->ssl, NULL);
    SSL_free(c->ssl);
    h3_pool_release(c);
    return NULL;
  }
  tp.stateless_reset_token_present = 1;

  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_addr_init(&ps.path.local, local, locallen);
  ngtcp2_addr_init(&ps.path.remote, peer, peerlen);

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
  if (h3_cid_add(c, &c->scid) != 0 || h3_hash_insert(&c->odcid, c) != 0) {
    h3conn_del(c);
    return NULL;
  }
  return c;
}

void h3conn_del(h3_conn_t *c) {
  if (!c) return;
  h3_llhls_on_conn_close(c);
  h3_hls_cold_on_conn_close(c);
  h3_ws_on_conn_close(c);
  if (c->h3conn) {
    nghttp3_conn_del(c->h3conn);
    c->h3conn = NULL;
  }
  for (int i = 0; i < c->max_reqs; i++) {
    h3_req_t *r = &c->reqs[i];
    if (!r->active) continue;
    hls_resp_body_release(r->resp_data, r->resp_zc);
    r->resp_data = NULL;
    if (r->tspush_sub_idx >= 0) ts_push_unsubscribe_by_idx(r->tspush_sub_idx);
    if (r->dashchunk_sub_idx >= 0) dash_lldash_sub_close(r->dashchunk_sub_idx);
    if (r->mp4push_sub_idx >= 0) mp4push_sub_close(r->mp4push_sub_idx);
    free(r->path);
    r->path = NULL;
    r->active = 0;
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

  for (int i = 0; i < c->ncids; i++) h3_hash_delete(&c->cids[i], c);
  h3_hash_delete(&c->odcid, c);
  int last = --t_h3_active_cnt;
  if (c->active_idx != last) {
    t_h3_active[c->active_idx] = t_h3_active[last];
    t_h3_active[c->active_idx]->active_idx = c->active_idx;
  }
  t_h3_active[last] = NULL;
  h3_pool_release(c);
}

#endif /* HAVE_HTTP3 */
