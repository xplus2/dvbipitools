/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "h3_rig.h"

#define ID_BYTES 32
#define BIDI_SID 400
#define UNI_SID 402

static h3_conn_t *server_conn(void) {
  ck_assert_int_eq(t_h3_active_cnt, 1);
  return t_h3_active[0];
}

static h3_conn_t *lookup_cid(const ngtcp2_cid *cid) {
  uint8_t pkt[48] = {0x40};

  memcpy(pkt + 1, cid->data, cid->datalen);
  return find_conn(pkt, sizeof pkt);
}

START_TEST(rand_callback_fills_the_buffer) {
  uint8_t buf[ID_BYTES] = {0};
  int nonzero = 0;

  cb_rand(buf, sizeof buf, NULL);
  for (size_t i = 0; i < sizeof buf; i++) nonzero |= buf[i] != 0;
  ck_assert_int_eq(nonzero, 1);
}
END_TEST

START_TEST(new_connection_ids_are_registered_until_the_limit) {
  h3rig_t h;
  h3_conn_t *c;
  ngtcp2_cid cid;
  ngtcp2_stateless_reset_token token;
  int before;

  h3r_open(&h);
  c = server_conn();
  before = c->ncids;
  memset(&cid, 0, sizeof cid);
  ck_assert_int_eq(cb_get_new_connection_id2(c->qconn, &cid, &token, H3_SCID_LEN, c), 0);
  ck_assert_uint_eq(cid.datalen, (size_t)H3_SCID_LEN);
  ck_assert_int_eq(c->ncids, before + 1);
  ck_assert_ptr_eq(lookup_cid(&cid), c);

  ck_assert_int_eq(cb_get_new_connection_id2(c->qconn, &cid, &token, 1, c), NGTCP2_ERR_CALLBACK_FAILURE);
  ck_assert_int_eq(c->ncids, before + 1);
  while (c->ncids < H3_MAX_CIDS) ck_assert_int_eq(cb_get_new_connection_id2(c->qconn, &cid, &token, H3_SCID_LEN, c), 0);
  ck_assert_int_eq(cb_get_new_connection_id2(c->qconn, &cid, &token, H3_SCID_LEN, c), NGTCP2_ERR_CALLBACK_FAILURE);
  ck_assert_int_eq(c->ncids, H3_MAX_CIDS);
  h3r_close(&h);
}
END_TEST

START_TEST(removed_connection_ids_stop_resolving) {
  h3rig_t h;
  h3_conn_t *c;
  ngtcp2_cid cid;
  ngtcp2_stateless_reset_token token;
  int before;

  h3r_open(&h);
  c = server_conn();
  ck_assert_int_eq(cb_get_new_connection_id2(c->qconn, &cid, &token, H3_SCID_LEN, c), 0);
  before = c->ncids;
  ck_assert_int_eq(cb_remove_connection_id(c->qconn, &cid, c), 0);
  ck_assert_int_eq(c->ncids, before - 1);
  ck_assert_ptr_null(lookup_cid(&cid));
  ck_assert_int_eq(cb_remove_connection_id(c->qconn, &cid, c), 0);
  ck_assert_int_eq(c->ncids, before - 1);
  h3r_close(&h);
}
END_TEST

typedef struct {
  const char *name;
  ngtcp2_path_validation_result res;
  uint32_t flags;
  socklen_t addrlen;
  int expect_update;
} path_case_t;

static const path_case_t path_cases[] = {
    {"success", NGTCP2_PATH_VALIDATION_RESULT_SUCCESS, 0, sizeof(struct sockaddr_in), 1},
    {"failure", NGTCP2_PATH_VALIDATION_RESULT_FAILURE, 0, sizeof(struct sockaddr_in), 0},
    {"aborted", NGTCP2_PATH_VALIDATION_RESULT_ABORTED, 0, sizeof(struct sockaddr_in), 0},
    {"preferred address", NGTCP2_PATH_VALIDATION_RESULT_SUCCESS, NGTCP2_PATH_VALIDATION_FLAG_PREFERRED_ADDR, sizeof(struct sockaddr_in), 0},
    {"oversized address", NGTCP2_PATH_VALIDATION_RESULT_SUCCESS, 0, sizeof(struct sockaddr_storage) + 8, 0},
};

START_TEST(path_validation_only_adopts_successful_new_paths) {
  const path_case_t *pc = &path_cases[_i];
  h3rig_t h;
  h3_conn_t *c;
  struct sockaddr_in moved;
  struct sockaddr_storage before;
  socklen_t before_len;
  ngtcp2_path path;

  h3r_open(&h);
  c = server_conn();
  before = c->peer_addr;
  before_len = c->peer_addrlen;
  h3r_sockaddr(&moved, 5555);
  memset(&path, 0, sizeof path);
  path.remote.addr = (ngtcp2_sockaddr *)&moved;
  path.remote.addrlen = pc->addrlen;
  ck_assert_int_eq(cb_path_validation(c->qconn, pc->flags, &path, NULL, pc->res, c), 0);
  if (pc->expect_update) {
    ck_assert_int_eq(((struct sockaddr_in *)&c->peer_addr)->sin_port, htons(5555));
  } else {
    ck_assert_mem_eq(&c->peer_addr, &before, sizeof before);
    ck_assert_uint_eq(c->peer_addrlen, before_len);
  }
  h3r_close(&h);
}
END_TEST

START_TEST(stream_open_allocates_requests_only_for_client_bidirectional_streams) {
  h3rig_t h;
  h3_conn_t *c;

  h3r_open(&h);
  c = server_conn();
  ck_assert_int_eq(cb_stream_open(c->qconn, UNI_SID, c), 0);
  ck_assert_ptr_null(find_req(c, UNI_SID));
  ck_assert_int_eq(cb_stream_open(c->qconn, BIDI_SID, c), 0);
  ck_assert_ptr_nonnull(find_req(c, BIDI_SID));
  ck_assert_int_eq(find_req(c, BIDI_SID)->tspush_sub_idx, -1);
  h3r_close(&h);
}
END_TEST

START_TEST(stream_close_frees_the_request_and_tolerates_unknown_streams) {
  h3rig_t h;
  h3_conn_t *c;

  h3r_open(&h);
  c = server_conn();
  ck_assert_int_eq(cb_stream_open(c->qconn, BIDI_SID, c), 0);
  ck_assert_ptr_nonnull(find_req(c, BIDI_SID));
  ck_assert_int_eq(cb_stream_close(c->qconn, 0, BIDI_SID, 0, c, NULL), 0);
  ck_assert_ptr_null(find_req(c, BIDI_SID));
  ck_assert_int_eq(cb_stream_close(c->qconn, 0, BIDI_SID + 4, 0, c, NULL), 0);
  h3r_close(&h);
}
END_TEST

START_TEST(stream_data_without_an_http3_layer_is_ignored) {
  h3rig_t h;
  h3_conn_t *c;
  nghttp3_conn *saved;
  static const uint8_t junk[] = {0x07, 0x00};

  h3r_open(&h);
  c = server_conn();
  saved = c->h3conn;
  c->h3conn = NULL;
  ck_assert_int_eq(cb_recv_stream_data(c->qconn, 0, 0, 0, junk, sizeof junk, c, NULL), 0);
  ck_assert_int_eq(cb_acked_stream_data_offset(c->qconn, 0, 0, 10, c, NULL), 0);
  c->h3conn = saved;
  h3r_close(&h);
}
END_TEST

START_TEST(malformed_stream_data_fails_the_callback) {
  h3rig_t h;
  h3_conn_t *c;
  static const uint8_t bad_frame[] = {0x00, 0x01, 0x78};

  h3r_open(&h);
  c = server_conn();
  ck_assert_int_eq(cb_recv_stream_data(c->qconn, 0, 0, 0, bad_frame, sizeof bad_frame, c, NULL), NGTCP2_ERR_CALLBACK_FAILURE);
  h3r_close(&h);
}
END_TEST

START_TEST(flush_with_nothing_pending_keeps_the_connection_alive) {
  h3rig_t h;
  h3_conn_t *c;

  h3r_open(&h);
  c = server_conn();
  flush_tx(c, h.srv_fd);
  flush_tx(c, h.srv_fd);
  ck_assert_int_eq(c->done, 0);
  h3r_close(&h);
}
END_TEST

START_TEST(flush_after_a_shutdown_notice_keeps_the_connection_alive) {
  h3rig_t h;
  h3_conn_t *c;

  h3r_open(&h);
  c = server_conn();
  ck_assert_int_eq(nghttp3_conn_submit_shutdown_notice(c->h3conn), 0);
  flush_tx(c, h.srv_fd);
  ck_assert_int_eq(c->done, 0);
  h3r_close(&h);
}
END_TEST

static Suite *callbacks_suite(void) {
  Suite *s = suite_create("dipixy_h3_callbacks");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, rand_callback_fills_the_buffer);
  tcase_add_test(tc, new_connection_ids_are_registered_until_the_limit);
  tcase_add_test(tc, removed_connection_ids_stop_resolving);
  tcase_add_loop_test(tc, path_validation_only_adopts_successful_new_paths, 0, (int)(sizeof path_cases / sizeof path_cases[0]));
  tcase_add_test(tc, stream_open_allocates_requests_only_for_client_bidirectional_streams);
  tcase_add_test(tc, stream_close_frees_the_request_and_tolerates_unknown_streams);
  tcase_add_test(tc, stream_data_without_an_http3_layer_is_ignored);
  tcase_add_test(tc, malformed_stream_data_fails_the_callback);
  tcase_add_test(tc, flush_with_nothing_pending_keeps_the_connection_alive);
  tcase_add_test(tc, flush_after_a_shutdown_notice_keeps_the_connection_alive);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(callbacks_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
