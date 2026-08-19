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

/* regression test for a real crash: a PMT signaling BISS Mode 1/E (ca_system_id 0x2602)
   with a real (non-null) pid on its program_info CA_descriptor gets that pid classified
   PID_ECM same as any other CAS scheme's ECM pid (see psi's add_ecm()). BISS 1/E never
   sets lc->dev (only the classic ECM/EMM branch does) or lc->biss_ca (that's BISS Mode
   CA). Before the fix, pkt_cb's ECM dispatch checked only lc->biss_ca before falling
   through to handle_ecm_section(), which immediately dereferences lc->dev in
   device_resolve_cw() -> device.c's service_slot() -> SIGSEGV on real traffic (a false-
   positive section-shaped match on live ES bytes, see biss_tvhead_plumbing_validation.sh). */
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

  /* even-parity ECM-shaped section landing on the now-classified ECM pid. payload
     must clear device_resolve_cw()'s own length floor (5 + CRYPTO_CW_ENC_LEN) or it
     returns early on that check alone, never reaching the lc->dev dereference */
  slen = build_bare_section(sec, 0x80, 24);
  wrap_section_packet(pkt, ECM_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_uint_eq(g_lc.ecm_pid, ECM_PID);
  ck_assert_int_eq(g_lc.fatal, 0);

  teardown();
}
END_TEST

/* same bug, EMM side: emmcache_feed(lc->cache, lc->dev, ...) was called unconditionally
   too, dereferencing lc->dev==NULL in device_on_emm(). emm_pid comes from any CAT, entirely
   independent of which CAS branch resolved lc->scr. */
START_TEST(biss1e_emm_on_null_dev_does_not_crash) {
  unsigned char pkt[188], sec[64];
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
