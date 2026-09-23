/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../http3.h"
#include "../http3_int.h"
#include "../http3_steer.h"

#include <openssl/rand.h>
#include <string.h>

void cb_rand(uint8_t *dest, size_t destlen, const ngtcp2_rand_ctx *ctx) {
  (void)ctx;
  RAND_bytes(dest, (int)destlen);
}

int cb_get_new_connection_id2(ngtcp2_conn *qconn, ngtcp2_cid *cid, ngtcp2_stateless_reset_token *token, size_t cidlen, void *ud) {
  (void)qconn;
  if (RAND_bytes(cid->data, (int)cidlen) != 1) return NGTCP2_ERR_CALLBACK_FAILURE;
  if (cidlen < H3_STEER_TAG_LEN) return NGTCP2_ERR_CALLBACK_FAILURE;
  h3_steer_tag_cid(cid->data);
  cid->datalen = cidlen;
  if (h3_reset_token(token->data, cid) != 0 || h3_cid_add(ud, cid) != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
  return 0;
}

int cb_remove_connection_id(ngtcp2_conn *qconn, const ngtcp2_cid *cid, void *ud) {
  (void)qconn;
  h3_cid_remove(ud, cid);
  return 0;
}

int cb_path_validation(ngtcp2_conn *qconn, uint32_t flags, const ngtcp2_path *path, const ngtcp2_path *fallback, ngtcp2_path_validation_result res, void *ud) {
  h3_conn_t *c = ud;
  (void)qconn;
  (void)fallback;
  if (res != NGTCP2_PATH_VALIDATION_RESULT_SUCCESS || (flags & NGTCP2_PATH_VALIDATION_FLAG_PREFERRED_ADDR)) return 0;
  if (path->remote.addrlen > sizeof c->peer_addr) return 0;
  memcpy(&c->peer_addr, path->remote.addr, path->remote.addrlen);
  c->peer_addrlen = path->remote.addrlen;
  return 0;
}

int cb_recv_stream_data(ngtcp2_conn *qconn, uint32_t flags, int64_t stream_id, uint64_t offset, const uint8_t *data, size_t datalen, void *ud, void *stream_ud) {
  (void)offset;
  (void)stream_ud;
  h3_conn_t *c = ud;
  if (!c->h3conn) return 0;
  nghttp3_ssize consumed = nghttp3_conn_read_stream(c->h3conn, stream_id, data, datalen, (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0);
  if (consumed < 0) return NGTCP2_ERR_CALLBACK_FAILURE;
  ngtcp2_conn_extend_max_stream_offset(qconn, stream_id, (uint64_t)consumed);
  ngtcp2_conn_extend_max_offset(qconn, (uint64_t)consumed);
  return 0;
}

int cb_acked_stream_data_offset(ngtcp2_conn *qconn, int64_t stream_id, uint64_t offset, uint64_t datalen, void *ud, void *stream_ud) {
  (void)qconn;
  (void)offset;
  (void)stream_ud;
  h3_conn_t *c = ud;
  if (c->h3conn && nghttp3_conn_add_ack_offset(c->h3conn, stream_id, datalen) != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
  return 0;
}

int cb_stream_open(ngtcp2_conn *qconn, int64_t stream_id, void *ud) {
  (void)qconn;
  if ((stream_id & 0x02) == 0) alloc_req(ud, stream_id);
  return 0;
}

int cb_stream_close(ngtcp2_conn *qconn, uint32_t flags, int64_t stream_id, uint64_t app_err, void *ud, void *stream_ud) {
  (void)flags;
  (void)app_err;
  (void)stream_ud;
  h3_conn_t *c = ud;
  if (c->h3conn) nghttp3_conn_close_stream(c->h3conn, stream_id, app_err);
  h3_llhls_on_stream_close(c, stream_id);
  h3_hls_cold_on_stream_close(c, stream_id);
  h3_ws_on_stream_close(c, stream_id);
  free_req(c, stream_id);
  if ((stream_id & 0x03) == 0) ngtcp2_conn_extend_max_streams_bidi(qconn, 1);
  return 0;
}

#endif /* HAVE_HTTP3 */
