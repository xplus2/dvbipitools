/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "h3_rig.h"

#define INITIAL_MAX 2048
#define FAIL_PUMP_MS 600
#define MIN_INITIAL 1200

static void server_only(h3rig_t *h) {
  memset(h, 0, sizeof *h);
  h->srv_fd = -1;
  h->cli_fd = -1;
  h3r_server_start(h);
}

static int cond_never_wrap(h3rig_t *h, void *arg) {
  (void)h;
  (void)arg;
  return 0;
}

static void send_to_server(const h3rig_t *h, const uint8_t *data, size_t len) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq((int)sendto(fd, data, len, 0, (const struct sockaddr *)&h->srv_addr, sizeof h->srv_addr), (int)len);
  close(fd);
}

static void step_server(const h3rig_t *h, int rounds) {
  for (int i = 0; i < rounds; i++) h3r_server_step(h);
}

START_TEST(unusable_credentials_leave_the_quic_context_unbuilt) {
  char dir[] = "/tmp/dipixy_h3tls_XXXXXX";
  char cert_a[64];
  char key_a[64];
  char cert_b[64];
  char key_b[64];
  char garbage[64];
  FILE *f;
  char cmd[96];

  ck_assert_ptr_nonnull(mkdtemp(dir));
  snprintf(cert_a, sizeof cert_a, "%s/a.crt", dir);
  snprintf(key_a, sizeof key_a, "%s/a.key", dir);
  snprintf(cert_b, sizeof cert_b, "%s/b.crt", dir);
  snprintf(key_b, sizeof key_b, "%s/b.key", dir);
  snprintf(garbage, sizeof garbage, "%s/garbage.pem", dir);
  tls_fixture_write_cert(cert_a, key_a);
  tls_fixture_write_cert(cert_b, key_b);
  f = fopen(garbage, "wb");
  ck_assert_ptr_nonnull(f);
  fputs("not a certificate\n", f);
  fclose(f);

  ck_assert_int_eq(h3_ready(), 0);
  h3_init("/nonexistent/cert.pem", key_a);
  ck_assert_int_eq(h3_ready(), 0);
  h3_init(cert_a, "/nonexistent/key.pem");
  ck_assert_int_eq(h3_ready(), 0);
  h3_init(garbage, key_a);
  ck_assert_int_eq(h3_ready(), 0);
  h3_init(cert_a, garbage);
  ck_assert_int_eq(h3_ready(), 0);
  h3_init(cert_a, key_b);
  ck_assert_int_eq(h3_ready(), 0);
  ck_assert_int_lt(h3_create_udp_sock(0, "127.0.0.1"), 0);
  ck_assert_int_lt(h3_create_udp_sock6(0, "::1"), 0);
  h3_init(cert_a, key_a);
  ck_assert_int_eq(h3_ready(), 1);
  h3_cleanup();
  ck_assert_int_eq(h3_ready(), 0);
  h3_cleanup();
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  ck_assert_int_eq(system(cmd), 0);
}
END_TEST

typedef struct {
  const char *name;
  const unsigned char *alpn;
  unsigned alpn_len;
  int no_alpn;
} alpn_case_t;

static const alpn_case_t alpn_cases[] = {
    {"http/2 only", (const unsigned char *)"\x02h2", 3, 0},
    {"http/1.1 only", (const unsigned char *)"\x08http/1.1", 9, 0},
    {"no alpn extension", NULL, 0, 1},
    {"close but not h3", (const unsigned char *)"\x03h3x", 4, 0},
};

START_TEST(client_hello_without_the_h3_protocol_is_refused) {
  const alpn_case_t *ac = &alpn_cases[_i];
  h3rig_t h;

  memset(&h, 0, sizeof h);
  h.srv_fd = -1;
  h.cli_fd = -1;
  h.alpn = ac->alpn;
  h.alpn_len = ac->alpn_len;
  h.no_alpn = ac->no_alpn;
  h3r_server_start(&h);
  h3r_client_start(&h);
  (void)h3r_pump_timed(&h, cond_never_wrap, NULL, FAIL_PUMP_MS);
  step_server(&h, 20);
  ck_assert_int_eq(t_h3_init ? t_h3_active_cnt : 0, 0);
  h3r_close(&h);
}
END_TEST

START_TEST(non_quic_datagrams_create_no_connections) {
  h3rig_t h;
  uint8_t junk[INITIAL_MAX];
  static const size_t sizes[] = {1, 2, 17, 64, 1199, 1200, 1500};

  server_only(&h);
  for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37 + 11);
  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
    send_to_server(&h, junk, sizes[i]);
    step_server(&h, 3);
  }
  junk[0] = 0xC0;
  memset(junk + 1, 0xFF, 4);
  send_to_server(&h, junk, MIN_INITIAL);
  step_server(&h, 3);
  junk[0] = 0x40;
  send_to_server(&h, junk, 64);
  step_server(&h, 3);
  ck_assert_int_eq(t_h3_init ? t_h3_active_cnt : 0, 0);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(truncated_initial_packets_are_dropped) {
  h3rig_t h;
  h3rig_t cli;
  uint8_t pkt[INITIAL_MAX];
  size_t len;
  static const size_t cuts[] = {5, 40, 300, 900};

  server_only(&h);
  memset(&cli, 0, sizeof cli);
  cli.srv_fd = -1;
  cli.cli_fd = -1;
  len = h3r_initial_packet(&cli, pkt, sizeof pkt);
  ck_assert_uint_ge(len, (size_t)MIN_INITIAL);
  for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
    send_to_server(&h, pkt, cuts[i]);
    step_server(&h, 3);
  }
  ck_assert_int_eq(t_h3_init ? t_h3_active_cnt : 0, 0);
  h3r_client_free(&cli);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(corrupted_client_hello_never_survives_as_a_connection) {
  h3rig_t h;
  h3rig_t cli;
  uint8_t pkt[INITIAL_MAX];
  size_t len;
  static const size_t flips[] = {30, 100, 600, 1100};

  server_only(&h);
  memset(&cli, 0, sizeof cli);
  cli.srv_fd = -1;
  cli.cli_fd = -1;
  len = h3r_initial_packet(&cli, pkt, sizeof pkt);
  for (size_t i = 0; i < sizeof flips / sizeof flips[0]; i++) {
    uint8_t copy[INITIAL_MAX];

    memcpy(copy, pkt, len);
    copy[flips[i]] ^= 0xA5;
    send_to_server(&h, copy, len);
    step_server(&h, 4);
    ck_assert_int_eq(t_h3_init ? t_h3_active_cnt : 0, 0);
  }
  h3r_client_free(&cli);
  h3r_server_stop(&h);
}
END_TEST

START_TEST(server_recovers_and_serves_after_hostile_handshakes) {
  h3rig_t h;
  uint8_t junk[INITIAL_MAX];
  int64_t sid;

  memset(&h, 0, sizeof h);
  h.srv_fd = -1;
  h.cli_fd = -1;
  h.no_alpn = 1;
  h3r_server_start(&h);
  h3r_client_start(&h);
  (void)h3r_pump_timed(&h, h3r_cond_handshake, NULL, FAIL_PUMP_MS);
  h3r_client_free(&h);
  memset(junk, 0x5A, sizeof junk);
  send_to_server(&h, junk, MIN_INITIAL);
  step_server(&h, 5);
  h.no_alpn = 0;
  memset(h.resp, 0, sizeof h.resp);
  h.handshake_done = 0;
  h.closed_by_peer = 0;
  h3r_client_start(&h);
  ck_assert_int_eq(h3r_pump_until(&h, h3r_cond_handshake, NULL), 1);
  ck_assert_int_eq(h3r_pump_until(&h, h3r_cond_server_ready_multi, NULL), 1);
  sid = h3r_request(&h, "GET", "/no/such/route", NULL, 0);
  ck_assert_int_eq(h3r_wait_response(&h, sid), 1);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, 404);
  h3r_close(&h);
}
END_TEST

static Suite *tls_suite(void) {
  Suite *s = suite_create("dipixy_h3_tls");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, unusable_credentials_leave_the_quic_context_unbuilt);
  tcase_add_loop_test(tc, client_hello_without_the_h3_protocol_is_refused, 0, (int)(sizeof alpn_cases / sizeof alpn_cases[0]));
  tcase_add_test(tc, non_quic_datagrams_create_no_connections);
  tcase_add_test(tc, truncated_initial_packets_are_dropped);
  tcase_add_test(tc, corrupted_client_hello_never_survives_as_a_connection);
  tcase_add_test(tc, server_recovers_and_serves_after_hostile_handshakes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tls_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
