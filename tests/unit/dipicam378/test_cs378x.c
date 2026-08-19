/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipicam378/cs378x/cs378x.h"

START_TEST(crc32_matches_standard_check_value) {
  /* "123456789" -> 0xCBF43926 is the universal CRC-32/ISO-HDLC check value,
     confirms this is the same reflected/0xEDB88320/0xFFFFFFFF variant oscam uses */
  const unsigned char data[] = "123456789";
  cs378x_crc32_init_table();
  ck_assert_uint_eq(cs378x_crc32(data, 9), 0xCBF43926u);
}
END_TEST

START_TEST(crc32_of_empty_is_zero) {
  cs378x_crc32_init_table();
  ck_assert_uint_eq(cs378x_crc32((const unsigned char *)"", 0), 0);
}
END_TEST

START_TEST(md5_matches_known_vectors) {
  unsigned char out[16];
  const unsigned char md5_empty[16] = {
      0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
      0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
  const unsigned char md5_abc[16] = {
      0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
      0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};

  ck_assert_int_eq(cs378x_md5((const unsigned char *)"", 0, out), 0);
  ck_assert_mem_eq(out, md5_empty, 16);

  ck_assert_int_eq(cs378x_md5((const unsigned char *)"abc", 3, out), 0);
  ck_assert_mem_eq(out, md5_abc, 16);
}
END_TEST

START_TEST(aes128_ecb_roundtrip) {
  unsigned char key[16];
  unsigned char buf[32], orig[32];
  int i;

  for (i = 0; i < 16; i++)
    key[i] = (unsigned char)(i * 11);
  for (i = 0; i < 32; i++)
    buf[i] = (unsigned char)(i ^ 0x5A);
  memcpy(orig, buf, sizeof buf);

  ck_assert_int_eq(cs378x_aes128_ecb(key, buf, sizeof buf, 1), 0);
  ck_assert_mem_ne(buf, orig, sizeof buf); /* actually changed */
  ck_assert_int_eq(cs378x_aes128_ecb(key, buf, sizeof buf, 0), 0);
  ck_assert_mem_eq(buf, orig, sizeof buf); /* recovered */
}
END_TEST

START_TEST(aes128_ecb_rejects_non_block_length) {
  unsigned char key[16] = {0};
  unsigned char buf[10] = {0};
  ck_assert_int_eq(cs378x_aes128_ecb(key, buf, sizeof buf, 1), -1);
}
END_TEST

START_TEST(frame_boundary_rounds_up_to_16) {
  ck_assert_uint_eq(cs378x_frame_boundary(1), 16);
  ck_assert_uint_eq(cs378x_frame_boundary(16), 16);
  ck_assert_uint_eq(cs378x_frame_boundary(17), 32);
  ck_assert_uint_eq(cs378x_frame_boundary(32), 32);
  ck_assert_uint_eq(cs378x_frame_boundary(36), 48);
}
END_TEST

static unsigned test_free_port(void) {
  struct sockaddr_in addr;
  socklen_t len = sizeof addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  unsigned port = 0;

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &len), 0);
  port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

static int connect_loopback(unsigned port) {
  struct sockaddr_in addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((unsigned short)port);

  for (int i = 0; i < 40; i++) {
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
      return fd;
    if (errno != ECONNREFUSED && errno != EINPROGRESS)
      break;
    usleep(25 * 1000);
  }

  close(fd);
  ck_abort_msg("failed to connect test client");
  return -1;
}

static void build_ecm_request(unsigned char frame[36], const char *password) {
  unsigned char key[16];
  unsigned char body[32];
  uint32_t crc;

  memset(frame, 0, 36);
  memset(body, 0, sizeof body);
  frame[0] = 1;
  frame[1] = 2;
  frame[2] = 3;
  frame[3] = 4;
  body[0] = 0;
  body[8] = 0x12;
  body[9] = 0x34;
  body[10] = 0x4A;
  body[11] = 0x75;
  body[20] = 0x80;
  body[21] = 0;
  body[22] = 0;
  crc = cs378x_crc32(body + 20, 3);
  body[4] = (unsigned char)(crc >> 24);
  body[5] = (unsigned char)(crc >> 16);
  body[6] = (unsigned char)(crc >> 8);
  body[7] = (unsigned char)crc;

  ck_assert_int_eq(cs378x_md5((const unsigned char *)password, strlen(password), key), 0);
  ck_assert_int_eq(cs378x_aes128_ecb(key, body, sizeof body, 1), 0);
  memcpy(frame + 4, body, sizeof body);
}

static int always_cw(const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned prid, unsigned char cw_out[16], void *user) {
  (void)ecm;
  (void)ecm_len;
  (void)srvid;
  (void)caid;
  (void)prid;
  (void)user;
  for (int i = 0; i < 16; i++)
    cw_out[i] = (unsigned char)(0xA0 + i);
  return 0;
}

static double monotonic_seconds(void) {
  struct timespec ts;
  ck_assert_int_eq(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int make_throttled_client(unsigned port) {
  struct timeval tv = {0, 100 * 1000};
  int fd = connect_loopback(port);
  int flags, rcvbuf = 4096;

  ck_assert_int_eq(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf), 0);
  ck_assert_int_eq(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv), 0);
  flags = fcntl(fd, F_GETFL, 0);
  ck_assert_int_ge(flags, 0);
  ck_assert_int_eq(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
  return fd;
}

/* floods ECM requests undrained until send buffer fills, parks worker mid blocking send */
static void saturate_ecm_response_path(int fd, const unsigned char frame[36]) {
  for (int i = 0; i < 200000; i++) {
    size_t sent = 0;
    while (sent < 36) {
      ssize_t n = send(fd, frame + sent, 36 - sent, MSG_DONTWAIT);
      if (n > 0) {
        sent += (size_t)n;
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        i = 200000;
        break;
      }
      ck_abort_msg("unexpected client send result saturating send buffer");
    }
  }
}

/* slow-reader fds: 1 response may be buffered pre-saturation, unread request
   backlog earns RST not FIN on close (normal TCP). both count as closed */
static void assert_closed_by_server(int fd) {
  double deadline = monotonic_seconds() + 2.0;
  for (;;) {
    struct pollfd pfd = {fd, POLLIN, 0};
    unsigned char buf[256];
    ssize_t rn;
    int remain_ms = (int)((deadline - monotonic_seconds()) * 1000.0);
    int pr;
    if (remain_ms <= 0)
      ck_abort_msg("fd %d: server never closed the connection", fd);
    pr = poll(&pfd, 1, remain_ms);
    ck_assert_msg(pr > 0, "fd %d: expected server-side close to be observable, poll returned %d", fd, pr);
    rn = read(fd, buf, sizeof buf);
    if (rn == 0)
      return;
    if (rn < 0 && errno == ECONNRESET)
      return;
    ck_assert_msg(rn > 0, "fd %d: unexpected read error, rn=%zd errno=%d", fd, rn, errno);
  }
}

START_TEST(server_stop_unblocks_slow_reader_worker) {
  static const char password[] = "secret";
  cs378x_cfg_t cfg;
  cs378x_server_t *srv;
  unsigned char frame[36];
  int cfd;
  double start, elapsed;
  unsigned port = test_free_port();

  memset(&cfg, 0, sizeof cfg);
  cfg.port = port;
  cfg.password = password;
  srv = cs378x_server_start(&cfg, always_cw, NULL, NULL);
  ck_assert_ptr_nonnull(srv);

  build_ecm_request(frame, password);
  cfd = make_throttled_client(port);
  saturate_ecm_response_path(cfd, frame);

  start = monotonic_seconds();
  cs378x_server_stop(srv);
  elapsed = monotonic_seconds() - start;
  ck_assert_msg(elapsed < 2.0, "server_stop took %.2fs", elapsed);

  close(cfd);
}
END_TEST

/* fills all CS378X_MAX_CONNS slots: idle, partial-recv, 2x blocked-send.
   confirms stop reaps each promptly and closes every fd */
START_TEST(server_stop_reaps_all_max_conns_in_mixed_states) {
  static const char password[] = "secret";
  cs378x_cfg_t cfg;
  cs378x_server_t *srv;
  unsigned char frame[36];
  int idle_fd, partial_fd, slow_fd1, slow_fd2;
  int fds[4];
  double start, elapsed;
  unsigned port = test_free_port();
  struct timespec settle = {0, 100L * 1000000L};

  memset(&cfg, 0, sizeof cfg);
  cfg.port = port;
  cfg.password = password;
  srv = cs378x_server_start(&cfg, always_cw, NULL, NULL);
  ck_assert_ptr_nonnull(srv);

  build_ecm_request(frame, password);

  idle_fd = connect_loopback(port);

  partial_fd = connect_loopback(port);
  ck_assert_int_eq(send(partial_fd, frame, 10, 0), 10); /* short of CS378X_MIN_FRAME (36) */

  slow_fd1 = make_throttled_client(port);
  saturate_ecm_response_path(slow_fd1, frame);
  slow_fd2 = make_throttled_client(port);
  saturate_ecm_response_path(slow_fd2, frame);

  nanosleep(&settle, NULL); /* workers reach parked state */

  start = monotonic_seconds();
  cs378x_server_stop(srv);
  elapsed = monotonic_seconds() - start;
  ck_assert_msg(elapsed < 2.0, "server_stop took %.2fs with 4 concurrent workers in mixed states", elapsed);

  fds[0] = idle_fd;
  fds[1] = partial_fd;
  fds[2] = slow_fd1;
  fds[3] = slow_fd2;
  for (int i = 0; i < 4; i++) {
    assert_closed_by_server(fds[i]);
    close(fds[i]);
  }
}
END_TEST

static Suite *cs378x_suite(void) {
  Suite *s = suite_create("cs378x");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, crc32_matches_standard_check_value);
  tcase_add_test(tc, crc32_of_empty_is_zero);
  tcase_add_test(tc, md5_matches_known_vectors);
  tcase_add_test(tc, aes128_ecb_roundtrip);
  tcase_add_test(tc, aes128_ecb_rejects_non_block_length);
  tcase_add_test(tc, frame_boundary_rounds_up_to_16);
  tcase_add_test(tc, server_stop_unblocks_slow_reader_worker);
  tcase_add_test(tc, server_stop_reaps_all_max_conns_in_mixed_states);
  tcase_set_timeout(tc, 10);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cs378x_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
