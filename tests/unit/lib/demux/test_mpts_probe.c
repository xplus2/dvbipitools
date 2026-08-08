/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/demux/crc32.h"
#include "lib/demux/mpts_probe.h"
#include "lib/mux/psi_build.h"

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

static void send_section(int sock, const struct sockaddr_in *dst, unsigned pid, const unsigned char *section, size_t slen) {
  unsigned char pkt[188];
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  memset(pkt + 5 + slen, 0xFF, 188 - 5 - slen);
  sendto(sock, pkt, 188, 0, (const struct sockaddr *)dst, sizeof *dst);
}

static tssrc_t *open_recv(const char *group, unsigned port) {
  tssrc_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = port;
  return tssrc_open(&cfg, NULL);
}

static int open_sender(const char *group, unsigned port, struct sockaddr_in *dst) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  memset(dst, 0, sizeof *dst);
  dst->sin_family = AF_INET;
  dst->sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, group, &dst->sin_addr);
  return sock;
}

START_TEST(mpts_probe_classifies_spts) {
  tssrc_t *src = open_recv("239.7.9.1", 15201);
  struct sockaddr_in dst;
  int sock = open_sender("239.7.9.1", 15201, &dst);
  unsigned char sec[256];
  size_t slen;
  mpts_probe_result_t r;

  ck_assert_ptr_nonnull(src);
  ck_assert_int_ge(sock, 0);

  slen = psi_build_pat(0x1234, 0, 7, 0x0100, sec, sizeof sec);
  send_section(sock, &dst, 0x0000, sec, slen);

  r = mpts_probe_run(src, 200);
  ck_assert_int_eq(r.kind, MPTS_PROBE_SPTS);
  ck_assert_int_eq(r.program_count, 1);

  close(sock);
  tssrc_close(src);
}
END_TEST

START_TEST(mpts_probe_classifies_mpts_with_names) {
  tssrc_t *src = open_recv("239.7.9.2", 15202);
  struct sockaddr_in dst;
  int sock = open_sender("239.7.9.2", 15202, &dst);
  unsigned char sec[256];
  size_t slen;
  psi_pat_entry_t progs[2];
  mpts_probe_result_t r;
  int k;

  ck_assert_ptr_nonnull(src);
  ck_assert_int_ge(sock, 0);

  progs[0].program_number = 101;
  progs[0].pmt_pid = 0x0100;
  progs[1].program_number = 102;
  progs[1].pmt_pid = 0x0200;
  slen = psi_build_pat_multi(0x1234, 0, progs, 2, sec, sizeof sec);
  send_section(sock, &dst, 0x0000, sec, slen);

  slen = build_pmt(sec, 101, 0x0101);
  send_section(sock, &dst, 0x0100, sec, slen);
  slen = build_pmt(sec, 102, 0x0201);
  send_section(sock, &dst, 0x0200, sec, slen);

  slen = psi_build_sdt(0, 0x1234, 5, 101, 0x01, "P", "One", sec, sizeof sec);
  send_section(sock, &dst, 0x0011, sec, slen);
  slen = psi_build_sdt(0, 0x1234, 5, 102, 0x01, "P", "Two", sec, sizeof sec);
  send_section(sock, &dst, 0x0011, sec, slen);

  r = mpts_probe_run(src, 500);
  ck_assert_int_eq(r.kind, MPTS_PROBE_MPTS);
  ck_assert_int_eq(r.program_count, 2);
  for (k = 0; k < 2; k++) {
    if (r.programs[k].program_number == 101) {
      ck_assert_uint_eq(r.programs[k].pmt_pid, 0x0100u);
      ck_assert_str_eq(r.programs[k].name, "One");
    } else {
      ck_assert_uint_eq(r.programs[k].program_number, 102u);
      ck_assert_uint_eq(r.programs[k].pmt_pid, 0x0200u);
      ck_assert_str_eq(r.programs[k].name, "Two");
    }
  }

  close(sock);
  tssrc_close(src);
}
END_TEST

START_TEST(mpts_probe_straggler_falls_back_to_empty_name) {
  tssrc_t *src = open_recv("239.7.9.3", 15203);
  struct sockaddr_in dst;
  int sock = open_sender("239.7.9.3", 15203, &dst);
  unsigned char sec[256];
  size_t slen;
  psi_pat_entry_t progs[2];
  mpts_probe_result_t r;

  ck_assert_ptr_nonnull(src);
  ck_assert_int_ge(sock, 0);

  progs[0].program_number = 201;
  progs[0].pmt_pid = 0x0100;
  progs[1].program_number = 202;
  progs[1].pmt_pid = 0x0200;
  slen = psi_build_pat_multi(0x1234, 0, progs, 2, sec, sizeof sec);
  send_section(sock, &dst, 0x0000, sec, slen);

  slen = build_pmt(sec, 201, 0x0101);
  send_section(sock, &dst, 0x0100, sec, slen);
  slen = build_pmt(sec, 202, 0x0201);
  send_section(sock, &dst, 0x0200, sec, slen);
  /* no SDT ever sent: both PMTs resolve, names stay "" past the short budget */

  r = mpts_probe_run(src, 200);
  ck_assert_int_eq(r.kind, MPTS_PROBE_MPTS);
  ck_assert_int_eq(r.program_count, 2);
  ck_assert_str_eq(r.programs[0].name, "");
  ck_assert_str_eq(r.programs[1].name, "");

  close(sock);
  tssrc_close(src);
}
END_TEST

static Suite *mpts_probe_suite(void) {
  Suite *s = suite_create("mpts_probe");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, mpts_probe_classifies_spts);
  tcase_add_test(tc, mpts_probe_classifies_mpts_with_names);
  tcase_add_test(tc, mpts_probe_straggler_falls_back_to_empty_name);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mpts_probe_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
