/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>
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
  ck_assert_uint_eq(cs378x_crc32(data, 9), 0xCBF43926u);
}
END_TEST

START_TEST(crc32_of_empty_is_zero) {
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

typedef struct {
  cam_auth_reason_t reason;
  const char *name;
} auth_reason_case_t;

static const auth_reason_case_t auth_reason_cases[] = {
  {CAM_AUTH_USER, "user"},
  {CAM_AUTH_CONNID, "connid"},
  {CAM_AUTH_CHECKSUM, "checksum"},
  {CAM_AUTH_OVERSIZED, "oversized"},
  {CAM_AUTH_REASON_COUNT, "unknown"},
  {(cam_auth_reason_t)99, "unknown"},
};

START_TEST(auth_reason_names_cover_every_enumerator) {
  ck_assert_str_eq(cs378x_auth_reason_name(auth_reason_cases[_i].reason), auth_reason_cases[_i].name);
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

#define TEST_PASSWORD "secret"
#define TEST_BUF 8192

typedef struct {
  atomic_int ecm_result;
  atomic_int emm_seen;
  atomic_uint emm_caid;
  atomic_uint emm_provid;
} cb_state_t;

static int scripted_ecm(const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned prid, unsigned char cw_out[16], void *user) {
  const cb_state_t *st = user;

  (void)ecm;
  (void)ecm_len;
  (void)srvid;
  (void)caid;
  (void)prid;
  for (int i = 0; i < 16; i++) cw_out[i] = (unsigned char)(0xA0 + i);
  return atomic_load(&st->ecm_result);
}

static void recording_emm(const unsigned char *emm, size_t emm_len, unsigned caid, unsigned provid, void *user) {
  cb_state_t *st = user;

  (void)emm;
  (void)emm_len;
  atomic_store(&st->emm_caid, caid);
  atomic_store(&st->emm_provid, provid);
  atomic_store(&st->emm_seen, 1);
}

static size_t build_frame(unsigned char *frame, const unsigned char ucrc[4], const char *password, unsigned cmd, const unsigned char *payload, size_t plen, unsigned len_byte) {
  unsigned char key[16];
  unsigned char body[TEST_BUF];
  size_t buflen = cmd == 0 ? 3 + (((size_t)(payload[1] & 0x0F) << 8) | payload[2]) : plen;
  size_t total = cs378x_frame_boundary(20 + buflen);
  uint32_t crc;

  if (total < 32) total = 32;
  memset(body, 0, sizeof body);
  body[0] = (unsigned char)cmd;
  body[1] = (unsigned char)len_byte;
  body[8] = 0x12;
  body[9] = 0x34;
  body[10] = 0x4A;
  body[11] = 0x75;
  body[15] = 0x01;
  memcpy(body + 20, payload, plen);
  crc = cs378x_crc32(body + 20, buflen);
  body[4] = (unsigned char)(crc >> 24);
  body[5] = (unsigned char)(crc >> 16);
  body[6] = (unsigned char)(crc >> 8);
  body[7] = (unsigned char)crc;
  ck_assert_int_eq(cs378x_md5((const unsigned char *)password, strlen(password), key), 0);
  ck_assert_int_eq(cs378x_aes128_ecb(key, body, total, 1), 0);
  memcpy(frame, ucrc, 4);
  memcpy(frame + 4, body, total);
  return 4 + total;
}

static size_t build_ecm(unsigned char *frame, const unsigned char ucrc[4], const char *password) {
  static const unsigned char section[] = {0x80, 0x00, 0x00};

  return build_frame(frame, ucrc, password, 0, section, sizeof section, 0);
}

static size_t build_simple(unsigned char *frame, const unsigned char ucrc[4], unsigned cmd, const unsigned char *payload, size_t plen) {
  return build_frame(frame, ucrc, TEST_PASSWORD, cmd, payload, plen, (unsigned)plen);
}

static void send_frame(int fd, const unsigned char *frame, size_t n) {
  ck_assert_int_eq((int)send(fd, frame, n, MSG_NOSIGNAL), (int)n);
}

static void send_ecm(int fd, const unsigned char ucrc[4], const char *password) {
  unsigned char frame[4 + TEST_BUF];
  size_t n = build_ecm(frame, ucrc, password);

  send_frame(fd, frame, n);
}

static void send_simple(int fd, const unsigned char ucrc[4], unsigned cmd, const unsigned char *payload, size_t plen) {
  unsigned char frame[4 + TEST_BUF];
  size_t n = build_simple(frame, ucrc, cmd, payload, plen);

  send_frame(fd, frame, n);
}

static int read_all_timed(int fd, unsigned char *buf, size_t n, int timeout_ms) {
  size_t got = 0;

  while (got < n) {
    struct pollfd pfd = {fd, POLLIN, 0};
    ssize_t r;

    if (poll(&pfd, 1, timeout_ms) <= 0) return -1;
    r = recv(fd, buf + got, n - got, 0);
    if (r <= 0) return -1;
    got += (size_t)r;
  }
  return 0;
}

static int read_reply(int fd, unsigned char *body, size_t body_len, int timeout_ms) {
  unsigned char key[16];
  unsigned char frame[4 + TEST_BUF];

  if (read_all_timed(fd, frame, 4 + body_len, timeout_ms) != 0) return -1;
  ck_assert_int_eq(cs378x_md5((const unsigned char *)TEST_PASSWORD, strlen(TEST_PASSWORD), key), 0);
  memcpy(body, frame + 4, body_len);
  ck_assert_int_eq(cs378x_aes128_ecb(key, body, body_len, 0), 0);
  return 0;
}

static int peer_closed(int fd) {
  unsigned char b[64];

  for (int i = 0; i < 30; i++) {
    struct pollfd pfd = {fd, POLLIN, 0};
    ssize_t r;

    if (poll(&pfd, 1, 100) <= 0) continue;
    r = recv(fd, b, sizeof b, 0);
    if (r == 0 || (r < 0 && errno == ECONNRESET)) return 1;
  }
  return 0;
}

static cs378x_server_t *start_server(const char *username, int verbose, cb_state_t *st, unsigned *port_out) {
  cs378x_cfg_t cfg;
  cs378x_server_t *srv;

  memset(&cfg, 0, sizeof cfg);
  cfg.port = test_free_port();
  cfg.password = TEST_PASSWORD;
  cfg.username = username;
  cfg.verbose = verbose;
  *port_out = cfg.port;
  srv = cs378x_server_start(&cfg, scripted_ecm, recording_emm, st);
  ck_assert_ptr_nonnull(srv);
  return srv;
}

static const unsigned char UCRC[4] = {1, 2, 3, 4};

START_TEST(ecm_request_gets_a_control_word) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server(NULL, 1, &st, &port);
  unsigned char body[48];
  cs378x_metrics_t m;
  int fd = connect_loopback(port);

  send_ecm(fd, UCRC, TEST_PASSWORD);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  ck_assert_int_eq(body[0], 1);
  ck_assert_int_eq(body[1], 16);
  for (int i = 0; i < 16; i++) ck_assert_int_eq(body[20 + i], 0xA0 + i);
  cs378x_server_get_metrics(srv, &m);
  ck_assert_uint_eq(m.ecm_total, 1u);
  ck_assert_uint_eq(m.ecm_errors_total, 0u);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(unsupported_caid_gets_the_stop_asking_answer) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv;
  unsigned char body[32];
  cs378x_metrics_t m;
  int fd;

  atomic_store(&st.ecm_result, -2);
  srv = start_server(NULL, 0, &st, &port);
  fd = connect_loopback(port);
  send_ecm(fd, UCRC, TEST_PASSWORD);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  ck_assert_int_eq(body[0], 8);
  ck_assert_int_eq(body[1], 2);
  cs378x_server_get_metrics(srv, &m);
  ck_assert_uint_eq(m.ecm_errors_total, 1u);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(transient_ecm_failure_gets_no_answer_but_keeps_the_connection) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv;
  unsigned char body[32];
  int fd;

  atomic_store(&st.ecm_result, -1);
  srv = start_server(NULL, 1, &st, &port);
  fd = connect_loopback(port);
  send_ecm(fd, UCRC, TEST_PASSWORD);
  ck_assert_int_ne(read_reply(fd, body, sizeof body, 400), 0);
  send_simple(fd, UCRC, 55, (const unsigned char *)"\0", 1);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  ck_assert_int_eq(body[0], 55);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(emm_commands_reach_the_callback_without_a_reply) {
  static const unsigned cmds[] = {6, 19};
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server(NULL, 1, &st, &port);
  unsigned char body[32];
  unsigned char payload[10] = {0x82, 0x70, 0x08, 1, 2, 3, 4, 5, 6, 7};
  cs378x_metrics_t m;
  int fd = connect_loopback(port);

  send_simple(fd, UCRC, cmds[_i], payload, sizeof payload);
  for (int i = 0; i < 100 && !atomic_load(&st.emm_seen); i++) usleep(10000);
  ck_assert_int_eq(atomic_load(&st.emm_seen), 1);
  ck_assert_uint_eq(atomic_load(&st.emm_caid), 0x4A75u);
  ck_assert_uint_eq(atomic_load(&st.emm_provid), 1u);
  ck_assert_int_ne(read_reply(fd, body, sizeof body, 300), 0);
  cs378x_server_get_metrics(srv, &m);
  ck_assert_uint_eq(m.emm_total, 1u);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(unknown_commands_are_ignored_and_the_connection_survives) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server(NULL, 1, &st, &port);
  unsigned char body[32];
  unsigned char payload[40];
  int fd = connect_loopback(port);
  memset(payload, 0x5A, sizeof payload);
  send_simple(fd, UCRC, 99, payload, 4);
  send_simple(fd, UCRC, 99, payload, sizeof payload);
  send_simple(fd, UCRC, 55, payload, 1);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  ck_assert_int_eq(body[0], 55);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(username_is_checked_when_configured) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server("alice", 0, &st, &port);
  unsigned char md5[16];
  unsigned char good[4];
  unsigned char body[48];
  uint32_t crc;
  cs378x_metrics_t m;
  int fd = connect_loopback(port);

  send_ecm(fd, UCRC, TEST_PASSWORD);
  ck_assert_int_eq(peer_closed(fd), 1);
  close(fd);
  ck_assert_int_eq(cs378x_md5((const unsigned char *)"alice", 5, md5), 0);
  crc = cs378x_crc32(md5, sizeof md5);
  good[0] = (unsigned char)(crc >> 24);
  good[1] = (unsigned char)(crc >> 16);
  good[2] = (unsigned char)(crc >> 8);
  good[3] = (unsigned char)crc;
  fd = connect_loopback(port);
  send_ecm(fd, good, TEST_PASSWORD);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  cs378x_server_get_metrics(srv, &m);
  ck_assert_uint_eq(m.auth_errors_total[CAM_AUTH_USER], 1u);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(changing_the_connection_id_closes_the_connection) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server(NULL, 0, &st, &port);
  unsigned char other[4] = {9, 9, 9, 9};
  unsigned char body[48];
  cs378x_metrics_t m;
  int fd = connect_loopback(port);

  send_ecm(fd, UCRC, TEST_PASSWORD);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  send_ecm(fd, other, TEST_PASSWORD);
  ck_assert_int_eq(peer_closed(fd), 1);
  cs378x_server_get_metrics(srv, &m);
  ck_assert_uint_eq(m.auth_errors_total[CAM_AUTH_CONNID], 1u);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(a_wrong_password_is_refused) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server(NULL, 0, &st, &port);
  unsigned char frame[4 + TEST_BUF];
  size_t n = build_ecm(frame, UCRC, "other-password");
  cs378x_metrics_t m;
  int fd = connect_loopback(port);

  memset(frame + n, 0, 2200);
  send_frame(fd, frame, n + 2200);
  ck_assert_int_eq(peer_closed(fd), 1);
  cs378x_server_get_metrics(srv, &m);
  ck_assert_uint_eq(m.auth_errors_total[CAM_AUTH_CHECKSUM] + m.auth_errors_total[CAM_AUTH_OVERSIZED], 1u);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(an_oversized_request_is_refused) {
  static const unsigned char section[] = {0x80, 0x0F, 0xFF};
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server(NULL, 0, &st, &port);
  unsigned char frame[4 + TEST_BUF];
  cs378x_metrics_t m;
  int fd = connect_loopback(port);

  build_frame(frame, UCRC, TEST_PASSWORD, 0, section, sizeof section, 0);
  send_frame(fd, frame, 36);
  ck_assert_int_eq(peer_closed(fd), 1);
  cs378x_server_get_metrics(srv, &m);
  ck_assert_uint_eq(m.auth_errors_total[CAM_AUTH_OVERSIZED], 1u);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(connections_beyond_the_limit_are_rejected) {
  cb_state_t st = {0};
  unsigned port;
  cs378x_server_t *srv = start_server(NULL, 0, &st, &port);
  unsigned char body[32];
  int fds[5];

  for (int i = 0; i < 4; i++) {
    fds[i] = connect_loopback(port);
    send_simple(fds[i], UCRC, 55, (const unsigned char *)"\0", 1);
    ck_assert_int_eq(read_reply(fds[i], body, sizeof body, 1500), 0);
  }
  fds[4] = connect_loopback(port);
  ck_assert_int_eq(peer_closed(fds[4]), 1);
  for (int i = 0; i < 5; i++) close(fds[i]);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(silent_connections_time_out_and_free_their_slots) {
  cs378x_cfg_t cfg;
  cs378x_server_t *srv;
  cb_state_t st = {0};
  unsigned char body[32];
  int idle[4];
  int fd;

  memset(&cfg, 0, sizeof cfg);
  cfg.port = test_free_port();
  cfg.password = TEST_PASSWORD;
  cfg.auth_timeout_ms = 300;
  cfg.idle_timeout_ms = 600;
  srv = cs378x_server_start(&cfg, scripted_ecm, recording_emm, &st);
  ck_assert_ptr_nonnull(srv);

  for (int i = 0; i < 4; i++) idle[i] = connect_loopback(cfg.port);
  for (int i = 0; i < 4; i++) {
    ck_assert_int_eq(peer_closed(idle[i]), 1);
    close(idle[i]);
  }

  fd = connect_loopback(cfg.port);
  send_simple(fd, UCRC, 55, (const unsigned char *)"\0", 1);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  ck_assert_int_eq(peer_closed(fd), 1);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(bind_address_is_honored) {
  static const char *const wild[] = {"127.0.0.1", "0.0.0.0"};
  cs378x_cfg_t cfg;
  cs378x_server_t *srv;
  cb_state_t st = {0};
  unsigned char body[48];
  int fd;

  memset(&cfg, 0, sizeof cfg);
  cfg.password = TEST_PASSWORD;
  cfg.bind = wild[_i];
  cfg.port = test_free_port();
  srv = cs378x_server_start(&cfg, scripted_ecm, recording_emm, &st);
  ck_assert_ptr_nonnull(srv);
  fd = connect_loopback(cfg.port);
  send_ecm(fd, UCRC, TEST_PASSWORD);
  ck_assert_int_eq(read_reply(fd, body, sizeof body, 1500), 0);
  close(fd);
  cs378x_server_stop(srv);
}
END_TEST

START_TEST(unusable_bind_address_fails_start) {
  static const char *const bad[] = {"192.0.2.1", "not a host name"};
  cs378x_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.password = TEST_PASSWORD;
  cfg.bind = bad[_i];
  cfg.port = test_free_port();
  ck_assert_ptr_null(cs378x_server_start(&cfg, scripted_ecm, NULL, NULL));
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
  tcase_add_loop_test(tc, auth_reason_names_cover_every_enumerator, 0, (int)(sizeof auth_reason_cases / sizeof auth_reason_cases[0]));
  tcase_add_test(tc, server_stop_unblocks_slow_reader_worker);
  tcase_add_test(tc, server_stop_reaps_all_max_conns_in_mixed_states);
  tcase_add_test(tc, ecm_request_gets_a_control_word);
  tcase_add_test(tc, unsupported_caid_gets_the_stop_asking_answer);
  tcase_add_test(tc, transient_ecm_failure_gets_no_answer_but_keeps_the_connection);
  tcase_add_loop_test(tc, emm_commands_reach_the_callback_without_a_reply, 0, 2);
  tcase_add_test(tc, unknown_commands_are_ignored_and_the_connection_survives);
  tcase_add_test(tc, username_is_checked_when_configured);
  tcase_add_test(tc, changing_the_connection_id_closes_the_connection);
  tcase_add_test(tc, a_wrong_password_is_refused);
  tcase_add_test(tc, an_oversized_request_is_refused);
  tcase_add_test(tc, connections_beyond_the_limit_are_rejected);
  tcase_add_test(tc, silent_connections_time_out_and_free_their_slots);
  tcase_add_loop_test(tc, bind_address_is_honored, 0, 2);
  tcase_add_loop_test(tc, unusable_bind_address_fails_start, 0, 2);
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
