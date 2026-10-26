/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"
#include "dipitvhead/tvhead/priv.h"
#include "../run_helper.h"
#include "../fx_net.h"
#include "../fx_ts.h"

/* PMT section (table_id 0x02), CRC included. with_es: one MPEG audio ES on pcr_pid */
static size_t build_pmt_local(unsigned char *out, unsigned prog_num, unsigned pcr_pid, int with_es) {
  unsigned char body[24];
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
  if (with_es) {
    body[n++] = 0x03;
    body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
    body[n++] = (unsigned char)pcr_pid;
    body[n++] = 0xF0;
    body[n++] = 0x00;
  }

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
  in.iface_in = "lo";
  return tvsrc_open(&cfg, &in, &reason);
}

START_TEST(discover_step_returns_zero_with_no_data_yet) {
  tvsrc_t *src = open_recv(run_helper_group_n(41), run_helper_port(41));
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
  tvsrc_t *src = open_recv(run_helper_group_n(42), run_helper_port(42));
  struct sockaddr_in dst;
  int sock = open_sender(run_helper_group_n(42), run_helper_port(42), &dst);
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
  wrap_section_packet(pat, 0x0000, sec, slen);
  slen = build_pmt_local(sec, 7, 0x0101, 1);
  wrap_section_packet(pmt, 0x0100, sec, slen);
  slen = psi_build_sdt(0, 0x1234, 5, 7, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_section_packet(sdt, 0x0011, sec, slen);

  /* queued in the kernel's UDP recv buffer before discover() ever calls tvsrc_read();
     psi_ready() only needs PAT+PMT, so discover() returns as soon as the 2nd packet is read */
  sendto(sock, pat, sizeof pat, 0, (const struct sockaddr *)&dst, sizeof dst);
  sendto(sock, pmt, sizeof pmt, 0, (const struct sockaddr *)&dst, sizeof dst);
  sendto(sock, sdt, sizeof sdt, 0, (const struct sockaddr *)&dst, sizeof dst);
  rc = discover(src, &in, psi, &im);
  close(sock);

  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(psi_program_number(psi), 7u);

  psi_free(psi);
  tvsrc_close(src);
}
END_TEST

START_TEST(discover_stays_pending_on_pmt_without_es) {
  tvsrc_t *src = open_recv(run_helper_group_n(44), run_helper_port(44));
  struct sockaddr_in dst;
  int sock = open_sender(run_helper_group_n(44), run_helper_port(44), &dst);
  dipitvhead_input_t in;
  discover_state_t ds;
  psi_t *psi = psi_new();
  input_metrics_t im;
  unsigned char sec[64];
  unsigned char pat[188];
  unsigned char pmt[188];
  size_t slen;

  memset(&in, 0, sizeof in);
  memset(&ds, 0, sizeof ds);
  memset(&im, 0, sizeof im);
  ck_assert_ptr_nonnull(src);

  slen = psi_build_pat(0x1234, 0, 7, 0x0100, sec, sizeof sec);
  wrap_section_packet(pat, 0x0000, sec, slen);
  slen = build_pmt_local(sec, 7, 0x0101, 0);
  wrap_section_packet(pmt, 0x0100, sec, slen);

  sendto(sock, pat, sizeof pat, 0, (const struct sockaddr *)&dst, sizeof dst);
  sendto(sock, pmt, sizeof pmt, 0, (const struct sockaddr *)&dst, sizeof dst);
  ck_assert_int_eq(discover_step(&ds, src, &in, psi, &im), 0);
  ck_assert_int_eq(discover_step(&ds, src, &in, psi, &im), 0);
  close(sock);

  psi_free(psi);
  tvsrc_close(src);
}
END_TEST

START_TEST(discover_fails_when_requested_pmt_pid_absent_from_pat) {
  tvsrc_t *src = open_recv(run_helper_group_n(43), run_helper_port(43));
  struct sockaddr_in dst;
  int sock = open_sender(run_helper_group_n(43), run_helper_port(43), &dst);
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
  wrap_section_packet(pat, 0x0000, sec, slen);

  sendto(sock, pat, sizeof pat, 0, (const struct sockaddr *)&dst, sizeof dst);
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
  tcase_add_test(tc, discover_stays_pending_on_pmt_without_es);
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
