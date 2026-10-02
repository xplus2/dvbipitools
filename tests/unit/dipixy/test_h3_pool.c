/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "h3_rig.h"

#define PKT_LEN 48
#define LONG_FORM_FLAG 0xC0
#define SHORT_FORM_FLAG 0x40
#define INITIAL_MAX 2048

static void server_with_capacity(h3rig_t *h, int max_conns) {
  memset(h, 0, sizeof *h);
  h->srv_fd = -1;
  h->cli_fd = -1;
  h3r_server_start(h);
  h3_set_max_conns_per_thread(max_conns);
}

static void make_cid(ngtcp2_cid *cid, unsigned seed) {
  uint8_t data[H3_SCID_LEN];

  for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(seed * 31u + i * 7u + 1u);
  ngtcp2_cid_init(cid, data, sizeof data);
}

static size_t short_packet(uint8_t *pkt, const ngtcp2_cid *cid) {
  memset(pkt, 0, PKT_LEN);
  pkt[0] = SHORT_FORM_FLAG;
  memcpy(pkt + 1, cid->data, cid->datalen);
  return PKT_LEN;
}

static size_t long_packet(uint8_t *pkt, const ngtcp2_cid *cid) {
  size_t pos = 0;

  memset(pkt, 0, PKT_LEN);
  pkt[pos++] = LONG_FORM_FLAG;
  pkt[pos++] = 0;
  pkt[pos++] = 0;
  pkt[pos++] = 0;
  pkt[pos++] = 1;
  pkt[pos++] = (uint8_t)cid->datalen;
  memcpy(pkt + pos, cid->data, cid->datalen);
  pos += cid->datalen;
  pkt[pos++] = 0;
  return PKT_LEN;
}

static h3_conn_t *lookup(const ngtcp2_cid *cid) {
  uint8_t pkt[PKT_LEN];
  size_t n = short_packet(pkt, cid);

  return find_conn(pkt, n);
}

START_TEST(tables_allocate_once_and_reset_on_free) {
  h3rig_t h;
  h3_conn_t **active;

  server_with_capacity(&h, 4);
  ck_assert_int_eq(t_h3_init, 0);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  ck_assert_int_eq(t_h3_init, 1);
  ck_assert_int_eq(t_h3_pool_free_n, 4);
  ck_assert_uint_eq(t_h3_hash_cap, 32u);
  active = t_h3_active;
  ck_assert_int_eq(h3_tables_alloc(), 1);
  ck_assert_ptr_eq(t_h3_active, active);
  h3_tables_free();
  ck_assert_int_eq(t_h3_init, 0);
  ck_assert_ptr_null(t_h3_pool);
  ck_assert_ptr_null(t_h3_hash);
  ck_assert_int_eq(t_h3_pool_free_n, 0);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  ck_assert_int_eq(t_h3_pool_free_n, 4);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(lookup_without_tables_finds_nothing) {
  h3rig_t h;
  ngtcp2_cid cid;

  server_with_capacity(&h, 4);
  make_cid(&cid, 1);
  ck_assert_ptr_null(lookup(&cid));
  h3r_server_stop(&h);
}
END_TEST

START_TEST(inserted_ids_are_found_by_short_and_long_headers) {
  h3rig_t h;
  ngtcp2_cid cid;
  ngtcp2_cid other;
  h3_conn_t conn = {0};
  uint8_t pkt[PKT_LEN];
  size_t n;

  server_with_capacity(&h, 4);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  make_cid(&cid, 1);
  make_cid(&other, 2);
  ck_assert_int_eq(h3_hash_insert(&cid, &conn), 0);
  ck_assert_ptr_eq(lookup(&cid), &conn);
  ck_assert_ptr_null(lookup(&other));
  n = long_packet(pkt, &cid);
  ck_assert_ptr_eq(find_conn(pkt, n), &conn);
  n = long_packet(pkt, &other);
  ck_assert_ptr_null(find_conn(pkt, n));
  ck_assert_ptr_null(find_conn(pkt, 1));
  h3r_server_stop(&h);
}
END_TEST

START_TEST(deleted_ids_leave_the_probe_chain_intact) {
  h3rig_t h;
  ngtcp2_cid cids[9];
  h3_conn_t conns[9];

  server_with_capacity(&h, 1);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  ck_assert_uint_eq(t_h3_hash_cap, 8u);
  memset(conns, 0, sizeof conns);
  for (unsigned i = 0; i < 9; i++) make_cid(&cids[i], i + 10);
  for (int i = 0; i < 8; i++) ck_assert_int_eq(h3_hash_insert(&cids[i], &conns[i]), 0);
  ck_assert_int_eq(h3_hash_insert(&cids[8], &conns[8]), -1);

  h3_hash_delete(&cids[3], &conns[3]);
  ck_assert_ptr_null(lookup(&cids[3]));
  for (int i = 0; i < 8; i++) {
    if (i != 3) ck_assert_ptr_eq(lookup(&cids[i]), &conns[i]);
  }
  ck_assert_int_eq(h3_hash_insert(&cids[8], &conns[8]), 0);
  ck_assert_ptr_eq(lookup(&cids[8]), &conns[8]);
  for (int i = 0; i < 8; i++) {
    if (i != 3) ck_assert_ptr_eq(lookup(&cids[i]), &conns[i]);
  }
  ck_assert_int_eq(h3_hash_insert(&cids[3], &conns[3]), -1);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(delete_needs_the_owning_connection) {
  h3rig_t h;
  ngtcp2_cid cid;
  h3_conn_t owner = {0};
  h3_conn_t stranger = {0};

  server_with_capacity(&h, 4);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  make_cid(&cid, 5);
  ck_assert_int_eq(h3_hash_insert(&cid, &owner), 0);
  h3_hash_delete(&cid, &stranger);
  ck_assert_ptr_eq(lookup(&cid), &owner);
  h3_hash_delete(&cid, &owner);
  ck_assert_ptr_null(lookup(&cid));
  h3r_server_stop(&h);
}
END_TEST

START_TEST(finished_connections_are_skipped) {
  h3rig_t h;
  ngtcp2_cid cid;
  h3_conn_t conn = {0};

  server_with_capacity(&h, 4);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  make_cid(&cid, 6);
  ck_assert_int_eq(h3_hash_insert(&cid, &conn), 0);
  conn.done = 1;
  ck_assert_ptr_null(lookup(&cid));
  conn.done = 0;
  ck_assert_ptr_eq(lookup(&cid), &conn);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(per_connection_id_limit_is_enforced) {
  h3rig_t h;
  ngtcp2_cid cids[H3_MAX_CIDS + 1];
  h3_conn_t conn = {0};

  server_with_capacity(&h, 4);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  for (unsigned i = 0; i <= H3_MAX_CIDS; i++) make_cid(&cids[i], i + 20);
  for (unsigned i = 0; i < H3_MAX_CIDS; i++) ck_assert_int_eq(h3_cid_add(&conn, &cids[i]), 0);
  ck_assert_int_eq(conn.ncids, H3_MAX_CIDS);
  ck_assert_int_eq(h3_cid_add(&conn, &cids[H3_MAX_CIDS]), -1);
  ck_assert_int_eq(conn.ncids, H3_MAX_CIDS);
  ck_assert_ptr_null(lookup(&cids[H3_MAX_CIDS]));
  h3r_server_stop(&h);
}
END_TEST

START_TEST(adding_an_id_fails_cleanly_when_the_table_is_full) {
  h3rig_t h;
  ngtcp2_cid cids[9];
  h3_conn_t fillers[8];
  h3_conn_t conn = {0};

  server_with_capacity(&h, 1);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  memset(fillers, 0, sizeof fillers);
  for (unsigned i = 0; i < 9; i++) make_cid(&cids[i], i + 40);
  for (int i = 0; i < 8; i++) ck_assert_int_eq(h3_hash_insert(&cids[i], &fillers[i]), 0);
  ck_assert_int_eq(h3_cid_add(&conn, &cids[8]), -1);
  ck_assert_int_eq(conn.ncids, 0);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(removing_ids_keeps_the_others_reachable) {
  h3rig_t h;
  ngtcp2_cid cids[4];
  ngtcp2_cid unknown;
  h3_conn_t conn = {0};

  server_with_capacity(&h, 4);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  for (unsigned i = 0; i < 4; i++) {
    make_cid(&cids[i], i + 60);
    ck_assert_int_eq(h3_cid_add(&conn, &cids[i]), 0);
  }
  make_cid(&unknown, 99);
  h3_cid_remove(&conn, &unknown);
  ck_assert_int_eq(conn.ncids, 4);
  h3_cid_remove(&conn, &cids[1]);
  ck_assert_int_eq(conn.ncids, 3);
  ck_assert_ptr_null(lookup(&cids[1]));
  ck_assert_ptr_eq(lookup(&cids[0]), &conn);
  ck_assert_ptr_eq(lookup(&cids[2]), &conn);
  ck_assert_ptr_eq(lookup(&cids[3]), &conn);
  h3_cid_remove(&conn, &cids[1]);
  ck_assert_int_eq(conn.ncids, 3);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(real_connections_exhaust_the_pool_and_reuse_freed_slots) {
  h3rig_t srv;
  h3rig_t cli[3];
  uint8_t pkt[3][INITIAL_MAX];
  size_t len[3];
  h3_conn_t *conns[3];

  server_with_capacity(&srv, 2);
  for (int i = 0; i < 3; i++) {
    memset(&cli[i], 0, sizeof cli[i]);
    cli[i].srv_fd = -1;
    cli[i].cli_fd = -1;
    len[i] = h3r_initial_packet(&cli[i], pkt[i], sizeof pkt[i]);
  }
  for (int i = 0; i < 2; i++) {
    conns[i] = h3conn_new(pkt[i], len[i], (struct sockaddr *)&cli[i].cli_addr, sizeof cli[i].cli_addr, (struct sockaddr *)&srv.srv_addr, sizeof srv.srv_addr, NULL);
    ck_assert_ptr_nonnull(conns[i]);
  }
  ck_assert_ptr_ne(conns[0], conns[1]);
  ck_assert_int_eq(t_h3_active_cnt, 2);
  ck_assert_int_eq(t_h3_pool_free_n, 0);
  conns[2] = h3conn_new(pkt[2], len[2], (struct sockaddr *)&cli[2].cli_addr, sizeof cli[2].cli_addr, (struct sockaddr *)&srv.srv_addr, sizeof srv.srv_addr, NULL);
  ck_assert_ptr_null(conns[2]);
  ck_assert_int_eq(t_h3_active_cnt, 2);
  ck_assert_int_eq(t_h3_pool_free_n, 0);

  h3conn_del(conns[0]);
  ck_assert_int_eq(t_h3_active_cnt, 1);
  ck_assert_int_eq(t_h3_pool_free_n, 1);
  conns[2] = h3conn_new(pkt[2], len[2], (struct sockaddr *)&cli[2].cli_addr, sizeof cli[2].cli_addr, (struct sockaddr *)&srv.srv_addr, sizeof srv.srv_addr, NULL);
  ck_assert_ptr_nonnull(conns[2]);
  ck_assert_int_eq(t_h3_active_cnt, 2);
  ck_assert_int_eq(t_h3_pool_free_n, 0);

  h3conn_del(conns[1]);
  h3conn_del(conns[2]);
  ck_assert_int_eq(t_h3_active_cnt, 0);
  ck_assert_int_eq(t_h3_pool_free_n, 2);
  for (int i = 0; i < 3; i++) h3r_client_free(&cli[i]);
  h3r_server_stop(&srv);
}
END_TEST

static Suite *pool_suite(void) {
  Suite *s = suite_create("dipixy_h3_pool");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, tables_allocate_once_and_reset_on_free);
  tcase_add_test(tc, lookup_without_tables_finds_nothing);
  tcase_add_test(tc, inserted_ids_are_found_by_short_and_long_headers);
  tcase_add_test(tc, deleted_ids_leave_the_probe_chain_intact);
  tcase_add_test(tc, delete_needs_the_owning_connection);
  tcase_add_test(tc, finished_connections_are_skipped);
  tcase_add_test(tc, per_connection_id_limit_is_enforced);
  tcase_add_test(tc, adding_an_id_fails_cleanly_when_the_table_is_full);
  tcase_add_test(tc, removing_ids_keeps_the_others_reachable);
  tcase_add_test(tc, real_connections_exhaust_the_pool_and_reuse_freed_slots);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pool_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
