/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "lib/net/ts/sink.h"

#define DGRAM_TS 1316
#define RX_TIMEOUT_MS 1000

static int make_rx(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  struct timeval tv = {RX_TIMEOUT_MS / 1000, 0};

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  ck_assert_int_eq(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static void fill_pattern(unsigned char *buf, size_t n) {
  for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)(i * 7 + 3);
}

static void make_tmp_path(char *path, size_t cap) {
  int fd;

  snprintf(path, cap, "/tmp/dvbipitools_tssink_XXXXXX");
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);
}

static size_t read_file(const char *path, unsigned char *buf, size_t cap) {
  int fd = open(path, O_RDONLY);
  ssize_t n;

  ck_assert_int_ge(fd, 0);
  n = read(fd, buf, cap);
  ck_assert_int_ge(n, 0);
  close(fd);
  return (size_t)n;
}

static tssink_cfg_t net_cfg(tssink_kind_t kind, unsigned port) {
  tssink_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = kind;
  cfg.family = AF_INET;
  cfg.group = "127.0.0.1";
  cfg.port = port;
  return cfg;
}

START_TEST(udp_write_splits_into_ts_datagrams) {
  unsigned port;
  int rx = make_rx(&port);
  tssink_cfg_t cfg = net_cfg(TSSINK_UDP, port);
  tssink_t *s;
  unsigned char data[2 * DGRAM_TS + 100];
  unsigned char buf[2048];
  static const size_t want[] = {DGRAM_TS, DGRAM_TS, 100};
  size_t off = 0;

  fill_pattern(data, sizeof data);
  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssink_write(s, data, sizeof data), 0);
  for (size_t i = 0; i < 3; i++) {
    ssize_t n = recv(rx, buf, sizeof buf, 0);

    ck_assert_int_eq((int)n, (int)want[i]);
    ck_assert_int_eq(memcmp(buf, data + off, want[i]), 0);
    off += want[i];
  }
  tssink_close(s);
  close(rx);
}
END_TEST

static void fill_ts(unsigned char *buf, size_t npkt) {
  for (size_t i = 0; i < npkt; i++) {
    memset(buf + i * 188, (int)(i & 0x3F), 188);
    buf[i * 188] = 0x47;
  }
}

START_TEST(packed_single_packet_writes_coalesce_to_full_datagrams) {
  unsigned port;
  int rx = make_rx(&port);
  tssink_cfg_t cfg = net_cfg(TSSINK_UDP, port);
  tssink_t *s;
  unsigned char data[14 * 188];
  unsigned char buf[2048];

  cfg.pack = 1;
  fill_ts(data, 14);
  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  for (size_t i = 0; i < 14; i++) ck_assert_int_eq(tssink_write(s, data + i * 188, 188), 0);
  for (size_t i = 0; i < 2; i++) {
    ck_assert_int_eq((int)recv(rx, buf, sizeof buf, 0), DGRAM_TS);
    ck_assert_int_eq(memcmp(buf, data + i * DGRAM_TS, DGRAM_TS), 0);
  }
  tssink_close(s);
  close(rx);
}
END_TEST

START_TEST(packed_unaligned_writes_yield_aligned_datagrams_and_flush) {
  unsigned port;
  int rx = make_rx(&port);
  tssink_cfg_t cfg = net_cfg(TSSINK_UDP, port);
  tssink_t *s;
  unsigned char data[9 * 188];
  unsigned char buf[2048];
  static const size_t cut[] = {44, 1052, 188 * 9 - 44 - 1052};
  size_t off = 0;

  cfg.pack = 1;
  fill_ts(data, 9);
  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  for (size_t i = 0; i < 3; i++) {
    ck_assert_int_eq(tssink_write(s, data + off, cut[i]), 0);
    off += cut[i];
  }
  ck_assert_int_eq((int)recv(rx, buf, sizeof buf, 0), DGRAM_TS);
  ck_assert_int_eq(memcmp(buf, data, DGRAM_TS), 0);
  ck_assert_int_eq(tssink_flush(s), 0);
  ck_assert_int_eq((int)recv(rx, buf, sizeof buf, 0), 2 * 188);
  ck_assert_int_eq(memcmp(buf, data + DGRAM_TS, 2 * 188), 0);
  tssink_close(s);
  close(rx);
}
END_TEST

START_TEST(rtp_write_prepends_header_with_incrementing_sequence) {
  unsigned port;
  int rx = make_rx(&port);
  tssink_cfg_t cfg = net_cfg(TSSINK_RTP, port);
  tssink_t *s;
  unsigned char data[3 * DGRAM_TS];
  unsigned char buf[2048];
  unsigned first_seq = 0;
  uint32_t ssrc = 0;

  fill_pattern(data, sizeof data);
  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssink_write(s, data, sizeof data), 0);
  for (unsigned i = 0; i < 3; i++) {
    ssize_t n = recv(rx, buf, sizeof buf, 0);
    unsigned seq = ((unsigned)buf[2] << 8) | buf[3];
    uint32_t this_ssrc = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) | ((uint32_t)buf[10] << 8) | buf[11];

    ck_assert_int_eq((int)n, 12 + DGRAM_TS);
    ck_assert_uint_eq(buf[0], 0x80);
    ck_assert_uint_eq(buf[1] & 0x7F, 33);
    if (i == 0) {
      first_seq = seq;
      ssrc = this_ssrc;
    }
    ck_assert_uint_eq(seq, (first_seq + i) & 0xFFFF);
    ck_assert_uint_eq(this_ssrc, ssrc);
    ck_assert_int_eq(memcmp(buf + 12, data + i * DGRAM_TS, DGRAM_TS), 0);
  }
  tssink_close(s);
  close(rx);
}
END_TEST

START_TEST(rtp_with_al_fec_sends_repair_on_fec_port) {
  unsigned port;
  unsigned fec_port;
  int rx = make_rx(&port);
  int fec_rx = make_rx(&fec_port);
  tssink_cfg_t cfg = net_cfg(TSSINK_RTP, port);
  tssink_t *s;
  unsigned char data[4 * DGRAM_TS];
  unsigned char buf[2048];

  cfg.al_fec_l = 2;
  cfg.al_fec_d = 2;
  cfg.al_fec_port = fec_port;
  fill_pattern(data, sizeof data);
  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssink_write(s, data, sizeof data), 0);
  for (unsigned i = 0; i < 4; i++) ck_assert_int_eq((int)recv(rx, buf, sizeof buf, 0), 12 + DGRAM_TS);
  for (unsigned i = 0; i < 2; i++) {
    ck_assert_int_eq((int)recv(fec_rx, buf, sizeof buf, 0), 28 + DGRAM_TS);
    ck_assert_uint_eq(buf[0] & 0xC0, 0x80);
    ck_assert_uint_eq(buf[1] & 0x7F, 96);
    ck_assert_uint_eq(buf[25], 2);
    ck_assert_uint_eq(buf[26], 2);
  }
  ck_assert_int_lt((int)recv(fec_rx, buf, sizeof buf, MSG_DONTWAIT), 0);
  tssink_close(s);
  close(rx);
  close(fec_rx);
}
END_TEST

START_TEST(file_sink_writes_and_truncates_on_reopen) {
  char path[64];
  tssink_cfg_t cfg;
  tssink_t *s;
  unsigned char data[1000];
  unsigned char back[2000];

  make_tmp_path(path, sizeof path);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSINK_FILE;
  cfg.file_path = path;
  fill_pattern(data, sizeof data);

  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssink_write(s, data, sizeof data), 0);
  ck_assert_int_eq(tssink_write(s, data, 500), 0);
  tssink_close(s);
  ck_assert_uint_eq(read_file(path, back, sizeof back), 1500u);
  ck_assert_int_eq(memcmp(back, data, 1000), 0);
  ck_assert_int_eq(memcmp(back + 1000, data, 500), 0);

  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssink_write(s, data, 10), 0);
  tssink_close(s);
  ck_assert_uint_eq(read_file(path, back, sizeof back), 10u);
  unlink(path);
}
END_TEST

START_TEST(stdout_sink_writes_to_fd_one_and_keeps_it_open) {
  char path[64];
  tssink_cfg_t cfg;
  tssink_t *s;
  unsigned char data[300];
  unsigned char back[400];
  int saved = dup(STDOUT_FILENO);
  int fd;

  ck_assert_int_ge(saved, 0);
  make_tmp_path(path, sizeof path);
  fd = open(path, O_WRONLY | O_TRUNC);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_ge(dup2(fd, STDOUT_FILENO), 0);
  close(fd);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSINK_STDOUT;
  fill_pattern(data, sizeof data);

  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssink_write(s, data, sizeof data), 0);
  tssink_close(s);
  ck_assert_int_eq((int)write(STDOUT_FILENO, "Z", 1), 1);
  ck_assert_int_ge(dup2(saved, STDOUT_FILENO), 0);
  close(saved);

  ck_assert_uint_eq(read_file(path, back, sizeof back), sizeof data + 1);
  ck_assert_int_eq(memcmp(back, data, sizeof data), 0);
  ck_assert_uint_eq(back[sizeof data], 'Z');
  unlink(path);
}
END_TEST

START_TEST(open_fails_cleanly_for_bad_targets) {
  tssink_cfg_t cfg = net_cfg(TSSINK_UDP, 5000);

  cfg.group = "not-an-address";
  ck_assert_ptr_null(tssink_open(&cfg));

  cfg = net_cfg(TSSINK_RTP, 5000);
  cfg.iface = "dvbipi-nonexistent0";
  ck_assert_ptr_null(tssink_open(&cfg));

  cfg = net_cfg(TSSINK_RTP, 5000);
  cfg.al_fec_l = 100;
  cfg.al_fec_d = 1;
  cfg.al_fec_port = 5002;
  ck_assert_ptr_null(tssink_open(&cfg));

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSINK_FILE;
  cfg.file_path = "/nonexistent-dir-dvbipitools/out.ts";
  ck_assert_ptr_null(tssink_open(&cfg));
}
END_TEST

START_TEST(write_to_full_device_reports_error) {
  tssink_cfg_t cfg;
  tssink_t *s;
  unsigned char data[188] = {0x47};

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSINK_FILE;
  cfg.file_path = "/dev/full";
  s = tssink_open(&cfg);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssink_write(s, data, sizeof data), -1);
  tssink_close(s);
}
END_TEST

static Suite *tssink_suite(void) {
  Suite *s = suite_create("tssink");
  TCase *tc = tcase_create("core");

  tcase_add_test(tc, udp_write_splits_into_ts_datagrams);
  tcase_add_test(tc, packed_single_packet_writes_coalesce_to_full_datagrams);
  tcase_add_test(tc, packed_unaligned_writes_yield_aligned_datagrams_and_flush);
  tcase_add_test(tc, rtp_write_prepends_header_with_incrementing_sequence);
  tcase_add_test(tc, rtp_with_al_fec_sends_repair_on_fec_port);
  tcase_add_test(tc, file_sink_writes_and_truncates_on_reopen);
  tcase_add_test(tc, stdout_sink_writes_to_fd_one_and_keeps_it_open);
  tcase_add_test(tc, open_fails_cleanly_for_bad_targets);
  tcase_add_test(tc, write_to_full_device_reports_error);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tssink_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
