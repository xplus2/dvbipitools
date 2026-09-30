/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipidescramble/pipeline.h"
#include "lib/demux/crc32.h"

#define PMT_PID 0x1000
#define ECM_PID 0x0064
#define EMM_PID 0x0065

static void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

static size_t build_pat(unsigned char *out, unsigned prog_num, unsigned pmt_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = 0x12;
  body[n++] = 0x34;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = (unsigned char)(0xE0 | ((pmt_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt_pid;

  hdr = n + 4;
  out[0] = 0x00;
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

/* one-program, zero-ES PMT with a CA_descriptor (tag 0x09) in program_info, CRC included */
static size_t build_pmt_with_ca(unsigned char *out, unsigned prog_num, unsigned ca_system_id, unsigned ca_pid) {
  unsigned char body[32];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0xE1; /* pcr_pid = 0x0101 */
  body[n++] = 0x01;
  body[n++] = 0xF0;
  body[n++] = 0x06;
  body[n++] = 0x09; /* CA_descriptor tag */
  body[n++] = 0x04;
  body[n++] = (unsigned char)(ca_system_id >> 8);
  body[n++] = (unsigned char)ca_system_id;
  body[n++] = (unsigned char)(0xE0 | ((ca_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)ca_pid;

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

/* CAT (table_id 0x01) with one CA_descriptor, CRC included */
static size_t build_cat(unsigned char *out, unsigned ca_system_id, unsigned emm_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = 0xFF;
  body[n++] = 0xFF;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0x09;
  body[n++] = 0x04;
  body[n++] = (unsigned char)(ca_system_id >> 8);
  body[n++] = (unsigned char)ca_system_id;
  body[n++] = (unsigned char)(0xE0 | ((emm_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)emm_pid;

  hdr = n + 4;
  out[0] = 0x01;
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

/* minimal section frame (table_id + 12-bit length, no CRC): psi_section_asm_feed only
   frames by declared length, doesn't check CRC. handle_ecm_section/emmcache_feed don't
   either - the fixed dispatch is gated on lc->dev, not on section well-formedness. */
static size_t build_bare_section(unsigned char *out, unsigned char table_id, size_t payload_len) {
  size_t n = 0;
  out[n++] = table_id;
  out[n++] = (unsigned char)(0x70 | ((payload_len >> 8) & 0x0F));
  out[n++] = (unsigned char)payload_len;
  memset(out + n, 0, payload_len);
  n += payload_len;
  return n;
}

static loop_ctx_t g_lc;
static config_t g_cfg;
static int g_devnull;

static void setup_biss1e(void) {
  memset(&g_lc, 0, sizeof g_lc);
  memset(&g_cfg, 0, sizeof g_cfg);
  g_cfg.biss2_sw_given = 1;
  memset(g_cfg.biss2_sw, 0xAB, sizeof g_cfg.biss2_sw);

  g_lc.cfg = &g_cfg;
  g_lc.psi = psi_new();
  ck_assert_ptr_nonnull(g_lc.psi);
  g_devnull = open("/dev/null", O_WRONLY);
  ck_assert_int_ge(g_devnull, 0);
  g_lc.outfd[0] = g_devnull;
  g_lc.n_outfd = 1;
}

static void teardown(void) {
  close(g_devnull);
  psi_free(g_lc.psi);
  ipiclient_free(g_lc.ipi);
  device_state_free(g_lc.dev);
  biss_ca_state_free(g_lc.biss_ca);
}

START_TEST(emit_downstream_writes_identical_bytes_to_every_outfd) {
  loop_ctx_t lc;
  config_t cfg;
  char path0[] = "/tmp/dipidescramble_test_outA_XXXXXX";
  char path1[] = "/tmp/dipidescramble_test_outB_XXXXXX";
  int fd0 = mkstemp(path0);
  int fd1 = mkstemp(path1);
  unsigned char pkt[188];
  unsigned char rd0[188 * 3], rd1[188 * 3];
  ssize_t n0, n1;

  ck_assert_int_ge(fd0, 0);
  ck_assert_int_ge(fd1, 0);
  memset(&lc, 0, sizeof lc);
  memset(&cfg, 0, sizeof cfg);
  lc.cfg = &cfg;
  lc.psi = psi_new();
  ck_assert_ptr_nonnull(lc.psi);
  lc.outfd[0] = fd0;
  lc.outfd[1] = fd1;
  lc.n_outfd = 2;

  for (int i = 0; i < 3; i++) {
    memset(pkt, (unsigned char)(0x10 + i), sizeof pkt);
    pkt[0] = 0x47;
    pkt[1] = 0x1F;
    pkt[2] = 0xFF;
    ck_assert_int_eq(pkt_cb(&lc, pkt), 0);
  }
  pipeline_flush(&lc);

  n0 = pread(fd0, rd0, sizeof rd0, 0);
  n1 = pread(fd1, rd1, sizeof rd1, 0);
  ck_assert_int_eq((int)n0, 188 * 3);
  ck_assert_int_eq((int)n1, 188 * 3);
  ck_assert_mem_eq(rd0, rd1, sizeof rd0);

  close(fd0);
  close(fd1);
  unlink(path0);
  unlink(path1);
  psi_free(lc.psi);
}
END_TEST

START_TEST(biss1e_ecm_on_null_dev_does_not_crash) {
  unsigned char pkt[188], sec[64];
  size_t slen;

  setup_biss1e();

  slen = build_pat(sec, 1, PMT_PID);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  slen = build_pmt_with_ca(sec, 1, 0x2602, ECM_PID);
  wrap_section_packet(pkt, PMT_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_ptr_nonnull(g_lc.scr); /* BISS 1/E resolved */
  ck_assert_ptr_null(g_lc.biss_ca);
  ck_assert_ptr_null(g_lc.dev); /* never set on this path - the crash's precondition */

  slen = build_bare_section(sec, 0x80, 24);
  wrap_section_packet(pkt, ECM_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_uint_eq(g_lc.ecm_pid, ECM_PID);
  ck_assert_int_eq(g_lc.fatal, 0);

  teardown();
}
END_TEST

START_TEST(biss1e_emm_on_null_dev_does_not_crash) {
  unsigned char pkt[188];
  unsigned char sec[64];
  size_t slen;
  setup_biss1e();
  slen = build_pat(sec, 1, PMT_PID);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  slen = build_pmt_with_ca(sec, 1, 0x2602, ECM_PID);
  wrap_section_packet(pkt, PMT_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  slen = build_cat(sec, 0x0B75, EMM_PID); /* unrelated ca_system_id, just needs a CAT present */
  wrap_section_packet(pkt, 0x0001, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_ptr_nonnull(g_lc.scr);
  ck_assert_ptr_null(g_lc.dev);
  ck_assert_uint_eq(g_lc.emm_pid, EMM_PID);

  slen = build_bare_section(sec, 0x82, 16);
  wrap_section_packet(pkt, EMM_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_int_eq(g_lc.fatal, 0);

  teardown();
}
END_TEST

static Suite *pipeline_suite(void) {
  Suite *s = suite_create("pipeline");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, emit_downstream_writes_identical_bytes_to_every_outfd);
  tcase_add_test(tc, biss1e_ecm_on_null_dev_does_not_crash);
  tcase_add_test(tc, biss1e_emm_on_null_dev_does_not_crash);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pipeline_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
