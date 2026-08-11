/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"
#include "dipitvhead/tvhead/priv.h"

static void wait_ms(int ms) {
  struct timespec ts = {0, (long)ms * 1000000L};
  nanosleep(&ts, NULL);
}

/* zero-ES PMT section (table_id 0x02), CRC included */
static size_t build_pmt(unsigned char *out, unsigned prog_num, unsigned pcr_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pcr_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;

  hdr = n + 4;
  out[0] = 0x02;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);

  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  size_t i;
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

static int open_sender(const char *group, unsigned port, struct sockaddr_in *dst) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  memset(dst, 0, sizeof *dst);
  dst->sin_family = AF_INET;
  dst->sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, group, &dst->sin_addr);
  return sock;
}

static tvsrc_t *open_recv(const char *group, unsigned port) {
  config_t cfg;
  dipitvhead_input_t in;
  net_err_reason_t reason = NET_ERR_OTHER;
  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  in.input.kind = SRC_UDP;
  in.input.family = AF_INET;
  strncpy(in.input.group, group, sizeof in.input.group - 1);
  in.input.port = port;
  return tvsrc_open(&cfg, &in, &reason);
}

START_TEST(discover_step_returns_zero_with_no_data_yet) {
  tvsrc_t *src = open_recv("239.7.9.41", 15341);
  dipitvhead_input_t in;
  discover_state_t ds;
  psi_t *psi = psi_new();
  input_metrics_t im;

  memset(&in, 0, sizeof in);
  memset(&ds, 0, sizeof ds);
  memset(&im, 0, sizeof im);
  ck_assert_ptr_nonnull(src);

  ck_assert_int_eq(discover_step(&ds, src, &in, psi, &im), 0);

  psi_free(psi);
  tvsrc_close(src);
}
END_TEST

START_TEST(discover_completes_once_pat_pmt_sdt_arrive) {
  tvsrc_t *src = open_recv("239.7.9.42", 15342);
  struct sockaddr_in dst;
  int sock = open_sender("239.7.9.42", 15342, &dst);
  dipitvhead_input_t in;
  psi_t *psi = psi_new();
  input_metrics_t im;
  unsigned char sec[64], pat[188], pmt[188], sdt[188];
  size_t slen;
  int rc;

  memset(&in, 0, sizeof in);
  memset(&im, 0, sizeof im);
  ck_assert_ptr_nonnull(src);

  slen = psi_build_pat(0x1234, 0, 7, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pat, 0x0000, sec, slen);
  slen = build_pmt(sec, 7, 0x0101);
  wrap_ts_packet(pmt, 0x0100, sec, slen);
  slen = psi_build_sdt(0, 0x1234, 5, 7, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(sdt, 0x0011, sec, slen);

  /* queued in the kernel's UDP recv buffer before discover() ever calls tvsrc_read();
     psi_ready() only needs PAT+PMT, so discover() returns as soon as the 2nd packet is read */
  sendto(sock, pat, sizeof pat, 0, (const struct sockaddr *)&dst, sizeof dst);
  sendto(sock, pmt, sizeof pmt, 0, (const struct sockaddr *)&dst, sizeof dst);
  sendto(sock, sdt, sizeof sdt, 0, (const struct sockaddr *)&dst, sizeof dst);
  wait_ms(50);
  rc = discover(src, &in, psi, &im);
  close(sock);

  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(psi_program_number(psi), 7u);

  psi_free(psi);
  tvsrc_close(src);
}
END_TEST

START_TEST(discover_fails_when_requested_pmt_pid_absent_from_pat) {
  tvsrc_t *src = open_recv("239.7.9.43", 15343);
  struct sockaddr_in dst;
  int sock = open_sender("239.7.9.43", 15343, &dst);
  dipitvhead_input_t in;
  psi_t *psi = psi_new();
  input_metrics_t im;
  unsigned char sec[64], pat[188];
  size_t slen;
  int rc;

  memset(&in, 0, sizeof in);
  memset(&im, 0, sizeof im);
  in.pmt_pid = 0x0200; /* not present in the PAT below */
  ck_assert_ptr_nonnull(src);

  slen = psi_build_pat(0x1234, 0, 7, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pat, 0x0000, sec, slen);

  sendto(sock, pat, sizeof pat, 0, (const struct sockaddr *)&dst, sizeof dst);
  wait_ms(50);
  rc = discover(src, &in, psi, &im);
  close(sock);

  ck_assert_int_eq(rc, -1);

  psi_free(psi);
  tvsrc_close(src);
}
END_TEST

static Suite *discover_suite(void) {
  Suite *s = suite_create("dipitvhead_discover");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 15);
  tcase_add_test(tc, discover_step_returns_zero_with_no_data_yet);
  tcase_add_test(tc, discover_completes_once_pat_pmt_sdt_arrive);
  tcase_add_test(tc, discover_fails_when_requested_pmt_pid_absent_from_pat);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(discover_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
