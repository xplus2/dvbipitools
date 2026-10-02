/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <time.h>

#include "h3_rig.h"

#define INITIAL_MAX 2048
#define CONNS 3
#define IDLE_NS 20000000ULL

static void server_only(h3rig_t *h, int max_conns) {
  memset(h, 0, sizeof *h);
  h->srv_fd = -1;
  h->cli_fd = -1;
  h3r_server_start(h);
  h3_set_max_conns_per_thread(max_conns);
}

static h3_conn_t *new_conn(h3rig_t *srv, h3rig_t *cli, const uint8_t *pkt, size_t len) {
  return h3conn_new(pkt, len, (struct sockaddr *)&cli->cli_addr, sizeof cli->cli_addr, (struct sockaddr *)&srv->srv_addr, sizeof srv->srv_addr, NULL);
}

static int cond_server_empty(h3rig_t *h, void *arg) {
  (void)h;
  (void)arg;
  return t_h3_init && t_h3_active_cnt == 0;
}

START_TEST(garbage_datagrams_never_consume_a_slot) {
  h3rig_t srv;
  h3rig_t cli;
  uint8_t pkt[INITIAL_MAX];
  size_t len;

  server_only(&srv, 2);
  memset(&cli, 0, sizeof cli);
  cli.srv_fd = -1;
  cli.cli_fd = -1;
  len = h3r_initial_packet(&cli, pkt, sizeof pkt);
  ck_assert_ptr_null(new_conn(&srv, &cli, pkt, 5));
  memset(pkt, 0xA5, 64);
  ck_assert_ptr_null(new_conn(&srv, &cli, pkt, 64));
  h3r_client_free(&cli);
  len = h3r_initial_packet(&cli, pkt, sizeof pkt);
  ck_assert_int_eq(t_h3_active_cnt, 0);
  ck_assert_int_eq(h3_tables_alloc(), 1);
  ck_assert_int_eq(t_h3_pool_free_n, 2);
  ck_assert_ptr_null(new_conn(&srv, &cli, pkt, 0));
  ck_assert_int_eq(t_h3_pool_free_n, 2);
  ck_assert_ptr_nonnull(new_conn(&srv, &cli, pkt, len));
  ck_assert_int_eq(t_h3_active_cnt, 1);
  h3r_client_free(&cli);
  h3r_server_stop(&srv);
}
END_TEST

START_TEST(deleting_in_any_order_keeps_the_active_list_dense) {
  h3rig_t srv;
  h3rig_t cli[CONNS];
  uint8_t pkt[CONNS][INITIAL_MAX];
  size_t len[CONNS];
  h3_conn_t *c[CONNS];
  static const int orders[3][CONNS] = {{0, 1, 2}, {2, 1, 0}, {1, 0, 2}};
  const int *order = orders[_i];

  server_only(&srv, CONNS);
  for (int i = 0; i < CONNS; i++) {
    memset(&cli[i], 0, sizeof cli[i]);
    cli[i].srv_fd = -1;
    cli[i].cli_fd = -1;
    len[i] = h3r_initial_packet(&cli[i], pkt[i], sizeof pkt[i]);
    c[i] = new_conn(&srv, &cli[i], pkt[i], len[i]);
    ck_assert_ptr_nonnull(c[i]);
  }
  for (int step = 0; step < CONNS; step++) {
    h3conn_del(c[order[step]]);
    ck_assert_int_eq(t_h3_active_cnt, CONNS - step - 1);
    ck_assert_int_eq(t_h3_pool_free_n, step + 1);
    for (int i = 0; i < t_h3_active_cnt; i++) {
      ck_assert_ptr_nonnull(t_h3_active[i]);
      ck_assert_int_eq(t_h3_active[i]->active_idx, i);
    }
    ck_assert_ptr_null(t_h3_active[t_h3_active_cnt]);
  }
  h3conn_del(NULL);
  for (int i = 0; i < CONNS; i++) h3r_client_free(&cli[i]);
  h3r_server_stop(&srv);
}
END_TEST

START_TEST(deleted_connections_drop_every_issued_id) {
  h3rig_t h;
  h3_conn_t *c;
  ngtcp2_cid scid;
  ngtcp2_cid odcid;

  h3r_open(&h);
  ck_assert_int_eq(t_h3_active_cnt, 1);
  c = t_h3_active[0];
  scid = c->scid;
  odcid = c->odcid;
  {
    uint8_t pkt[48] = {0x40};

    memcpy(pkt + 1, scid.data, scid.datalen);
    ck_assert_ptr_eq(find_conn(pkt, sizeof pkt), c);
    uint8_t lpkt[64] = {0xC0, 0, 0, 0, 1};

    lpkt[5] = (uint8_t)odcid.datalen;
    memcpy(lpkt + 6, odcid.data, odcid.datalen);
    ck_assert_ptr_eq(find_conn(lpkt, sizeof lpkt), c);
    h3conn_del(c);
    ck_assert_ptr_null(find_conn(lpkt, sizeof lpkt));
    ck_assert_ptr_null(find_conn(pkt, sizeof pkt));
  }
  ck_assert_int_eq(t_h3_active_cnt, 0);
  ck_assert_int_eq(t_h3_pool_free_n, g_h3_max_conns);
  h3r_close(&h);
}
END_TEST

START_TEST(client_close_releases_the_server_connection) {
  h3rig_t h;
  ngtcp2_path_storage ps;
  uint8_t buf[H3R_PKT];
  ngtcp2_ccerr err;
  ngtcp2_ssize n;

  h3r_open(&h);
  ck_assert_int_eq(t_h3_active_cnt, 1);
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_ccerr_default(&err);
  n = ngtcp2_conn_write_connection_close(h.qc, &ps.path, NULL, buf, sizeof buf, &err, h3_ts());
  ck_assert_int_gt((int)n, 0);
  ck_assert_int_eq((int)sendto(h.cli_fd, buf, (size_t)n, 0, (struct sockaddr *)&h.srv_addr, sizeof h.srv_addr), (int)n);
  ck_assert_int_eq(h3r_pump_until(&h, cond_server_empty, NULL), 1);
  ck_assert_int_eq(t_h3_pool_free_n, g_h3_max_conns);
  h3r_close(&h);
}
END_TEST

START_TEST(idle_connections_are_reaped_by_the_tick) {
  h3rig_t h;
  struct timespec ts = {0, 40000000};

  h3r_open(&h);
  ck_assert_int_eq(t_h3_active_cnt, 1);
  g_h3_idle_ns = IDLE_NS;
  nanosleep(&ts, NULL);
  h3_tick();
  ck_assert_int_eq(t_h3_active_cnt, 0);
  ck_assert_int_eq(t_h3_pool_free_n, g_h3_max_conns);
  h3r_close(&h);
}
END_TEST

START_TEST(thread_cleanup_releases_live_connections) {
  h3rig_t h;

  h3r_open(&h);
  ck_assert_int_eq(t_h3_active_cnt, 1);
  h3_thread_cleanup();
  ck_assert_int_eq(t_h3_init, 0);
  ck_assert_int_eq(t_h3_active_cnt, 0);
  h3_thread_cleanup();
  h3r_close(&h);
}
END_TEST

START_TEST(connect_and_close_cycles_never_over_release_the_pool) {
  h3rig_t srv;
  h3rig_t cli;
  uint8_t pkt[INITIAL_MAX];
  size_t len;

  server_only(&srv, 1);
  for (int round = 0; round < 40; round++) {
    h3_conn_t *c;

    memset(&cli, 0, sizeof cli);
    cli.srv_fd = -1;
    cli.cli_fd = -1;
    len = h3r_initial_packet(&cli, pkt, sizeof pkt);
    c = new_conn(&srv, &cli, pkt, len);
    ck_assert_ptr_nonnull(c);
    ck_assert_int_eq(t_h3_pool_free_n, 0);
    h3conn_del(c);
    ck_assert_int_eq(t_h3_pool_free_n, 1);
    h3r_client_free(&cli);
  }
  ck_assert_int_eq(t_h3_active_cnt, 0);
  h3r_server_stop(&srv);
}
END_TEST

START_TEST(timer_helpers_report_the_nearest_deadline) {
  h3rig_t h;
  int ms;

  ck_assert_int_eq(h3_next_timeout_ms(), -1);
  h3_tick();
  h3r_open(&h);
  ms = h3_next_timeout_ms();
  ck_assert_int_ge(ms, 0);
  ck_assert_int_le(ms, 1000);
  h3_thread_cleanup();
  ck_assert_int_eq(h3_next_timeout_ms(), -1);
  h3r_close(&h);
}
END_TEST

START_TEST(readable_handler_with_an_empty_socket_is_quiet) {
  h3rig_t h;

  server_only(&h, 2);
  h3_handle_readable(h.srv_fd);
  h3_handle_readable(h.srv_fd);
  ck_assert_int_eq(t_h3_active_cnt, 0);
  h3r_server_stop(&h);
}
END_TEST

static Suite *conn_suite(void) {
  Suite *s = suite_create("dipixy_h3_conn");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, garbage_datagrams_never_consume_a_slot);
  tcase_add_loop_test(tc, deleting_in_any_order_keeps_the_active_list_dense, 0, 3);
  tcase_add_test(tc, deleted_connections_drop_every_issued_id);
  tcase_add_test(tc, client_close_releases_the_server_connection);
  tcase_add_test(tc, idle_connections_are_reaped_by_the_tick);
  tcase_add_test(tc, thread_cleanup_releases_live_connections);
  tcase_add_test(tc, connect_and_close_cycles_never_over_release_the_pool);
  tcase_add_test(tc, timer_helpers_report_the_nearest_deadline);
  tcase_add_test(tc, readable_handler_with_an_empty_socket_is_quiet);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(conn_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
