/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/mux/fec2022.h"
#include "lib/net/multicast.h"
#include "lib/net/ts/source.h"

static tssrc_open_state_t drive(tssrc_open_t *o, int max_iters) {
  tssrc_open_state_t st = TSSRC_OPEN_PENDING;

  for (int i = 0; i < max_iters && st == TSSRC_OPEN_PENDING; i++) {
    struct pollfd pfd;
    int fd = tssrc_open_async_poll_fd(o);
    if (fd < 0) {
      st = tssrc_open_async_step(o, NULL);
      continue;
    }
    pfd.fd = fd;
    pfd.events = tssrc_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = tssrc_open_async_step(o, NULL);
  }
  return st;
}

static unsigned free_udp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static void unique_group(char *out, size_t cap) {
  pid_t pid = getpid();

  snprintf(out, cap, "239.77.%u.%u", ((unsigned)pid >> 8) & 0xFF, 1 + ((unsigned)pid & 0xFF) % 250);
}

static int open_sender(const char *group, unsigned port, struct sockaddr_in *dst) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  ck_assert_int_ge(sock, 0);
  memset(dst, 0, sizeof *dst);
  dst->sin_family = AF_INET;
  dst->sin_port = htons((unsigned short)port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &dst->sin_addr), 1);
  return sock;
}

static void build_rtp_ts(unsigned char *out, uint16_t seq, unsigned char id) {
  memset(out, 0xFF, 12 + 188);
  out[0] = 0x80;
  out[1] = 33;
  out[2] = (unsigned char)(seq >> 8);
  out[3] = (unsigned char)seq;
  out[11] = 9;
  out[12] = 0x47;
  out[13] = id;
}

START_TEST(tssrc_open_async_completes_immediately_for_udp) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;
  char group[32];

  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = free_udp_port();

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 5), TSSRC_OPEN_DONE);

  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tssrc_fd(s), 0);

  tssrc_close(s);
}
END_TEST

static uint64_t read_one_rx_ns(tssrc_t *s, int sock, const struct sockaddr_in *dst) {
  unsigned char pkt[188];
  unsigned char buf[2048];
  struct pollfd pfd;

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  ck_assert_int_eq((int)sendto(sock, pkt, sizeof pkt, 0, (const struct sockaddr *)dst, sizeof *dst), 188);
  pfd.fd = tssrc_fd(s);
  pfd.events = POLLIN;
  pfd.revents = 0;
  ck_assert_int_eq(poll(&pfd, 1, 1000), 1);
  ck_assert_int_eq((int)tssrc_read(s, buf, sizeof buf, NULL), 188);
  return tssrc_last_rx_ns(s);
}

START_TEST(tssrc_rx_timestamps_only_after_enable_and_advance) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  struct sockaddr_in dst;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  uint64_t first, second;
  char group[32];
  unsigned port = free_udp_port();

  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = port;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(sock, 0);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, group, &dst.sin_addr);

  ck_assert_uint_eq(read_one_rx_ns(s, sock, &dst), 0u);
  ck_assert_int_eq(tssrc_enable_rx_timestamps(s), 0);
  first = read_one_rx_ns(s, sock, &dst);
  ck_assert_uint_gt(first, 0u);
  usleep(20000);
  second = read_one_rx_ns(s, sock, &dst);
  ck_assert_uint_ge(second - first, 15000000u);

  close(sock);
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_jitter_reorders_rtp_and_delays_release) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  struct sockaddr_in dst;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  static const unsigned char order[3] = {1, 3, 2};
  unsigned char got[3];
  unsigned ngot = 0;
  struct timespec t0;
  struct timespec t1;
  long first_ms = -1;
  char group[32];
  unsigned port = free_udp_port();

  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_RTP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = port;
  cfg.jitter_ms = 60;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(sock, 0);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, group, &dst.sin_addr);

  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (unsigned i = 0; i < 3; i++) {
    unsigned char pkt[12 + 188];
    memset(pkt, 0xFF, sizeof pkt);
    pkt[0] = 0x80;
    pkt[1] = 33;
    pkt[3] = order[i];
    pkt[8] = 0;
    pkt[11] = 9;
    pkt[12] = 0x47;
    pkt[13] = order[i];
    ck_assert_int_eq((int)sendto(sock, pkt, sizeof pkt, 0, (const struct sockaddr *)&dst, sizeof dst), (int)sizeof pkt);
  }
  while (ngot < 3) {
    unsigned char buf[2048];
    struct pollfd pfd;
    ssize_t n;
    pfd.fd = tssrc_fd(s);
    pfd.events = POLLIN;
    pfd.revents = 0;
    ck_assert_int_ge(poll(&pfd, 1, 1000), 1);
    n = tssrc_read(s, buf, sizeof buf, NULL);
    ck_assert_int_ge((int)n, 0);
    if (n == 0) continue;
    ck_assert_int_eq((int)n, 188);
    if (first_ms < 0) {
      clock_gettime(CLOCK_MONOTONIC, &t1);
      first_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    }
    got[ngot++] = buf[1];
  }
  ck_assert_uint_eq(got[0], 1);
  ck_assert_uint_eq(got[1], 2);
  ck_assert_uint_eq(got[2], 3);
  ck_assert_int_ge((int)first_ms, 55);

  close(sock);
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_open_async_completes_immediately_for_stdin) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_STDIN;

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 5), TSSRC_OPEN_DONE);

  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssrc_fd(s), STDIN_FILENO);

  tssrc_close(s);
}
END_TEST

typedef struct {
  int listen_fd;
  const char *response;
  size_t response_len;
} server_arg_t;

static void *serve_once(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  char buf[4096];
  if (cfd < 0)
    return NULL;
  recv(cfd, buf, sizeof buf, 0);
  send(cfd, a->response, a->response_len, 0);
  close(cfd);
  return NULL;
}

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

START_TEST(tssrc_open_async_completes_for_http_and_reads_body) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  unsigned char ts_payload[3 * 188];
  char head[128];
  char resp[sizeof head + sizeof ts_payload];
  size_t head_len, resp_len;
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;
  unsigned char buf[1024];
  char uri[64];
  size_t got = 0;
  int tries = 0;

  memset(ts_payload, 0xFF, sizeof ts_payload);
  ts_payload[0] = 0x47;
  ts_payload[188] = 0x47;
  ts_payload[376] = 0x47;
  head_len = (size_t)snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n", sizeof ts_payload);
  memcpy(resp, head, head_len);
  memcpy(resp + head_len, ts_payload, sizeof ts_payload);
  resp_len = head_len + sizeof ts_payload;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = resp_len;
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), TSSRC_OPEN_DONE);

  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tssrc_fd(s), 0);

  while (got < sizeof ts_payload && tries++ < 200) {
    ssize_t n = tssrc_read(s, buf + got, sizeof buf - got, NULL);
    if (n > 0)
      got += (size_t)n;
    else if (n < 0)
      break;
    else
      usleep(5000);
  }
  ck_assert_uint_eq(got, sizeof ts_payload);
  ck_assert_int_eq(memcmp(buf, ts_payload, sizeof ts_payload), 0);

  tssrc_close(s);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(tssrc_open_async_reports_error_on_refused_connection) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/nothing", &cfg.http), 0); /* port 1: nothing listens here */

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), TSSRC_OPEN_ERROR);
  tssrc_open_async_free(o);
}
END_TEST

START_TEST(tssrc_mcast_returns_the_joined_socket_for_udp_only) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  char group[32];

  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = free_udp_port();
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_ptr_nonnull(tssrc_mcast(s));
  ck_assert_int_eq(mcast_fd(tssrc_mcast(s)), tssrc_fd(s));
  ck_assert_int_eq(tssrc_fec_fd(s), -1);
  tssrc_fec_poll(s);
  tssrc_close(s);

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_FILE;
  cfg.file_path = "/dev/null";
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_ptr_null(tssrc_mcast(s));
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_fec_poll_feeds_repair_and_recovers_lost_packet) {
  enum { SRC_PKTS = 4, SRC_LEN = 12 + 188 };
  tssrc_cfg_t cfg;
  tssrc_t *s;
  fec2022_enc_t *enc = fec2022_enc_new(2, 2, 96);
  struct sockaddr_in src_dst;
  struct sockaddr_in fec_dst;
  unsigned char pkt[SRC_PKTS][SRC_LEN];
  unsigned char repair[FEC2022_MAX_REPAIR];
  size_t rlen = 0;
  char group[32];
  unsigned port = free_udp_port();
  unsigned fec_port = free_udp_port();
  int src_sock;
  int fec_sock;
  unsigned char next[SRC_LEN];
  unsigned char buf[2048];

  ck_assert_ptr_nonnull(enc);
  ck_assert_uint_ne(port, fec_port);
  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_RTP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = port;
  cfg.al_fec_l = 2;
  cfg.al_fec_d = 2;
  cfg.al_fec_port = fec_port;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tssrc_fec_fd(s), 0);
  src_sock = open_sender(group, port, &src_dst);
  fec_sock = open_sender(group, fec_port, &fec_dst);

  for (unsigned i = 0; i < SRC_PKTS; i++) {
    build_rtp_ts(pkt[i], (uint16_t)(0x2000 + i), (unsigned char)i);
    rlen = fec2022_enc_feed(enc, pkt[i], SRC_LEN, 1000 + i, repair, sizeof repair);
  }
  ck_assert_uint_gt(rlen, 0u);
  for (unsigned i = 0; i < SRC_PKTS - 1; i++)
    ck_assert_int_eq((int)sendto(src_sock, pkt[i], SRC_LEN, 0, (struct sockaddr *)&src_dst, sizeof src_dst), SRC_LEN);
  for (unsigned i = 0; i < SRC_PKTS - 1; i++) {
    struct pollfd pfd = {tssrc_fd(s), POLLIN, 0};

    ck_assert_int_eq(poll(&pfd, 1, 1000), 1);
    ck_assert_int_ge((int)tssrc_read(s, buf, sizeof buf, NULL), 0);
  }
  ck_assert_int_eq((int)sendto(fec_sock, repair, rlen, 0, (struct sockaddr *)&fec_dst, sizeof fec_dst), (int)rlen);
  {
    struct pollfd fpfd = {tssrc_fec_fd(s), POLLIN, 0};

    ck_assert_int_eq(poll(&fpfd, 1, 1000), 1);
    tssrc_fec_poll(s);
  }
  for (unsigned i = 0; i < 3; i++) {
    build_rtp_ts(next, (uint16_t)(0x2000 + SRC_PKTS + i), (unsigned char)(SRC_PKTS + i));
    ck_assert_int_eq((int)sendto(src_sock, next, SRC_LEN, 0, (struct sockaddr *)&src_dst, sizeof src_dst), SRC_LEN);
  }
  for (unsigned i = 1; i <= 3; i++) {
    struct pollfd pfd = {tssrc_fd(s), POLLIN, 0};

    ck_assert_int_eq(poll(&pfd, 1, 1000), 1);
    ck_assert_int_eq((int)tssrc_read(s, buf, sizeof buf, NULL), 188);
    ck_assert_uint_eq(buf[0], 0x47);
    ck_assert_uint_eq(buf[1], i);
  }

  fec2022_enc_free(enc);
  close(src_sock);
  close(fec_sock);
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_read_consumes_repair_without_explicit_fec_poll) {
  enum { SRC_PKTS = 4, SRC_LEN = 12 + 188 };
  tssrc_cfg_t cfg;
  tssrc_t *s;
  fec2022_enc_t *enc = fec2022_enc_new(2, 2, 96);
  struct sockaddr_in src_dst;
  struct sockaddr_in fec_dst;
  unsigned char pkt[SRC_PKTS][SRC_LEN];
  unsigned char repair[FEC2022_MAX_REPAIR];
  size_t rlen = 0;
  char group[32];
  unsigned port = free_udp_port();
  unsigned fec_port = free_udp_port();
  int src_sock;
  int fec_sock;
  unsigned char next[SRC_LEN];
  unsigned char buf[2048];

  ck_assert_ptr_nonnull(enc);
  ck_assert_uint_ne(port, fec_port);
  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_RTP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = port;
  cfg.al_fec_l = 2;
  cfg.al_fec_d = 2;
  cfg.al_fec_port = fec_port;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tssrc_fec_fd(s), 0);
  src_sock = open_sender(group, port, &src_dst);
  fec_sock = open_sender(group, fec_port, &fec_dst);

  for (unsigned i = 0; i < SRC_PKTS; i++) {
    build_rtp_ts(pkt[i], (uint16_t)(0x2000 + i), (unsigned char)i);
    rlen = fec2022_enc_feed(enc, pkt[i], SRC_LEN, 1000 + i, repair, sizeof repair);
  }
  ck_assert_uint_gt(rlen, 0u);
  for (unsigned i = 0; i < SRC_PKTS - 1; i++)
    ck_assert_int_eq((int)sendto(src_sock, pkt[i], SRC_LEN, 0, (struct sockaddr *)&src_dst, sizeof src_dst), SRC_LEN);
  for (unsigned i = 0; i < SRC_PKTS - 1; i++) {
    struct pollfd pfd = {tssrc_fd(s), POLLIN, 0};

    ck_assert_int_eq(poll(&pfd, 1, 1000), 1);
    ck_assert_int_ge((int)tssrc_read(s, buf, sizeof buf, NULL), 0);
  }
  ck_assert_int_eq((int)sendto(fec_sock, repair, rlen, 0, (struct sockaddr *)&fec_dst, sizeof fec_dst), (int)rlen);
  {
    struct pollfd fpfd = {tssrc_fec_fd(s), POLLIN, 0};

    ck_assert_int_eq(poll(&fpfd, 1, 1000), 1);
  }
  for (unsigned i = 0; i < 3; i++) {
    build_rtp_ts(next, (uint16_t)(0x2000 + SRC_PKTS + i), (unsigned char)(SRC_PKTS + i));
    ck_assert_int_eq((int)sendto(src_sock, next, SRC_LEN, 0, (struct sockaddr *)&src_dst, sizeof src_dst), SRC_LEN);
  }
  for (unsigned i = 1; i <= 3; i++) {
    struct pollfd pfd = {tssrc_fd(s), POLLIN, 0};

    ck_assert_int_eq(poll(&pfd, 1, 1000), 1);
    ck_assert_int_eq((int)tssrc_read(s, buf, sizeof buf, NULL), 188);
    ck_assert_uint_eq(buf[0], 0x47);
    ck_assert_uint_eq(buf[1], i);
  }

  fec2022_enc_free(enc);
  close(src_sock);
  close(fec_sock);
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_read_with_fec_returns_zero_when_idle) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  char group[32];
  unsigned char buf[2048];
  struct timespec t0;
  struct timespec t1;

  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_RTP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = free_udp_port();
  cfg.al_fec_l = 2;
  cfg.al_fec_d = 2;
  cfg.al_fec_port = free_udp_port();
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  clock_gettime(CLOCK_MONOTONIC, &t0);
  ck_assert_int_eq((int)tssrc_read(s, buf, sizeof buf, NULL), 0);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  ck_assert_int_lt((int)(t1.tv_sec - t0.tv_sec), 1);
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_buffer_ms_is_minus_one_without_jitter_buffer) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  char group[32];

  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = free_udp_port();
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq((int)tssrc_buffer_ms(s), -1);
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_buffer_ms_reports_depth_with_jitter_buffer) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  struct sockaddr_in dst;
  unsigned char pkt[12 + 188];
  unsigned char buf[2048];
  char group[32];
  unsigned port = free_udp_port();
  int sock;
  int64_t depth;

  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_RTP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = port;
  cfg.jitter_ms = 500;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge((int)tssrc_buffer_ms(s), 0);
  sock = open_sender(group, port, &dst);
  build_rtp_ts(pkt, 1, 1);
  ck_assert_int_eq((int)sendto(sock, pkt, sizeof pkt, 0, (struct sockaddr *)&dst, sizeof dst), (int)sizeof pkt);
  for (int i = 0; i < 20; i++) {
    struct pollfd pfd = {tssrc_fd(s), POLLIN, 0};

    poll(&pfd, 1, 50);
    ck_assert_int_eq((int)tssrc_read(s, buf, sizeof buf, NULL), 0);
    if (tssrc_buffer_ms(s) > 0) break;
  }
  depth = tssrc_buffer_ms(s);
  ck_assert_int_gt((int)depth, 0);
  ck_assert_int_le((int)depth, 500);
  close(sock);
  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_jitter_source_failure_drains_then_reports_error) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  unsigned char buf[2048];
  char group[32];
  net_err_reason_t reason = NET_ERR_COUNT;
  int null_fd = open("/dev/null", O_RDONLY);
  int src_fd;

  ck_assert_int_ge(null_fd, 0);
  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = free_udp_port();
  cfg.jitter_ms = 50;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  src_fd = mcast_fd(tssrc_mcast(s));
  ck_assert_int_ge(dup2(null_fd, src_fd), 0);
  close(null_fd);

  ck_assert_int_eq((int)tssrc_read(s, buf, sizeof buf, &reason), -1);
  ck_assert_int_eq(reason, NET_ERR_READ);
  reason = NET_ERR_COUNT;
  ck_assert_int_eq((int)tssrc_read(s, buf, sizeof buf, &reason), -1);
  ck_assert_int_eq(reason, NET_ERR_READ);
  tssrc_close(s);
}
END_TEST

static Suite *tssource_async_suite(void) {
  Suite *s = suite_create("tssource_async");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tssrc_open_async_completes_immediately_for_udp);
  tcase_add_test(tc, tssrc_rx_timestamps_only_after_enable_and_advance);
  tcase_add_test(tc, tssrc_jitter_reorders_rtp_and_delays_release);
  tcase_add_test(tc, tssrc_open_async_completes_immediately_for_stdin);
  tcase_add_test(tc, tssrc_open_async_completes_for_http_and_reads_body);
  tcase_add_test(tc, tssrc_open_async_reports_error_on_refused_connection);
  tcase_add_test(tc, tssrc_mcast_returns_the_joined_socket_for_udp_only);
  tcase_add_test(tc, tssrc_fec_poll_feeds_repair_and_recovers_lost_packet);
  tcase_add_test(tc, tssrc_read_consumes_repair_without_explicit_fec_poll);
  tcase_add_test(tc, tssrc_read_with_fec_returns_zero_when_idle);
  tcase_add_test(tc, tssrc_buffer_ms_is_minus_one_without_jitter_buffer);
  tcase_add_test(tc, tssrc_buffer_ms_reports_depth_with_jitter_buffer);
  tcase_add_test(tc, tssrc_jitter_source_failure_drains_then_reports_error);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tssource_async_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
