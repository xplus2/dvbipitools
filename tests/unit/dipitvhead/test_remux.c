/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/remux.h"
#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"

#define MAX_SEEN 64

static unsigned g_pids[MAX_SEEN];
static unsigned char g_cc[MAX_SEEN];
static int g_count;

static void capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if (g_count < MAX_SEEN) {
    g_pids[g_count] = (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
    g_cc[g_count] = pkt[3] & 0x0F;
  }
  g_count++;
}

static int saw_pid(unsigned pid) {
  int i;
  for (i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == pid)
      return 1;
  return 0;
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

/* discovery psi_t with one video (0x0101, H264) and one audio (0x0102, AAC/eng) ES,
 * PAT program_number 101 -> PMT pid 0x0100 */
static psi_t *build_discovery_psi(void) {
  unsigned char section[256], pkt[188];
  size_t slen;
  psi_t *psi = psi_new();

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, section, sizeof section);
  wrap_ts_packet(pkt, 0x0000, section, slen);
  psi_feed(psi, pkt);

  {
    /* hand-build a 2-ES PMT: video H264 @0x101, audio AAC @0x102 lang "eng" */
    unsigned char body[64];
    size_t n = 0, hdr, crc_at;
    uint32_t crc;

    body[n++] = (unsigned char)(101 >> 8);
    body[n++] = (unsigned char)101;
    body[n++] = 0xC1;
    body[n++] = 0x00;
    body[n++] = 0x00;
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F); /* PCR pid = video */
    body[n++] = 0x01;
    body[n++] = 0xF0; /* program_info_length = 0 */
    body[n++] = 0x00;
    body[n++] = 0x1B; /* H264 */
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    body[n++] = 0x0F; /* AAC */
    body[n++] = 0xE0 | ((0x0102 >> 8) & 0x1F);
    body[n++] = 0x02;
    body[n++] = 0xF0;
    body[n++] = 0x06; /* ES_info_length = 6: one ISO 639 descriptor */
    body[n++] = 0x0A;
    body[n++] = 4;
    body[n++] = 'e';
    body[n++] = 'n';
    body[n++] = 'g';
    body[n++] = 0x00;

    hdr = n + 4;
    section[0] = 0x02;
    section[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
    section[2] = (unsigned char)hdr;
    memcpy(section + 3, body, n);
    crc_at = 3 + n;
    crc = crc32_mpeg(section, crc_at);
    section[crc_at + 0] = (unsigned char)(crc >> 24);
    section[crc_at + 1] = (unsigned char)(crc >> 16);
    section[crc_at + 2] = (unsigned char)(crc >> 8);
    section[crc_at + 3] = (unsigned char)crc;
    slen = crc_at + 4;
  }
  wrap_ts_packet(pkt, 0x0100, section, slen);
  psi_feed(psi, pkt);

  return psi;
}

static void base_cfg(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->sdt_mode = TABLE_DROP;
  cfg->nit_mode = TABLE_DROP;
  cfg->hbbtv_url = NULL;
  cfg->tsid = 1;
  cfg->onid = 2;
  cfg->sid = 101;
}

START_TEST(remux_forwards_mapped_es_and_sends_pat_pmt_on_first_feed) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  remux_t *r;
  unsigned char pkt[188];
  int i, video_idx = -1;

  base_cfg(&cfg);
  r = remux_new(&cfg, psi);
  ck_assert_ptr_nonnull(r);

  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x00 | ((0x0101 >> 8) & 0x1F));
  pkt[2] = (unsigned char)0x0101;
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, pkt, capture_cb, NULL);

  ck_assert(saw_pid(0x0000));  /* PAT */
  ck_assert(saw_pid(0x1000));  /* our PMT pid */
  ck_assert(saw_pid(0x0100));  /* video remapped to OUT_PID_VIDEO */

  for (i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == 0x0100)
      video_idx = i;
  ck_assert_int_ge(video_idx, 0);
  ck_assert_uint_eq(g_cc[video_idx], 1u); /* cc starts at 0, bumped once */

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_drops_unrecognized_pid_and_does_not_resend_psi_immediately) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  remux_t *r;
  unsigned char pkt[188];

  base_cfg(&cfg);
  r = remux_new(&cfg, psi);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  g_count = 0;
  remux_feed(r, pkt, capture_cb, NULL); /* primes last_pat etc */

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((0x0500 >> 8) & 0x1F); /* not carried in the ES map */
  pkt[2] = (unsigned char)0x0500;
  pkt[3] = 0x10;
  g_count = 0;
  remux_feed(r, pkt, capture_cb, NULL);

  ck_assert_int_eq(g_count, 0); /* PSI not due yet, pid not recognized: nothing emitted */

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_eit_passthrough_respects_strip_eit) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  remux_t *r;
  unsigned char pkt[188];

  base_cfg(&cfg);
  cfg.strip_eit = 0;
  r = remux_new(&cfg, psi);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = 0x12; /* EIT */
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, pkt, capture_cb, NULL);
  ck_assert(saw_pid(0x0012));

  remux_free(r);

  cfg.strip_eit = 1;
  r = remux_new(&cfg, psi);
  g_count = 0;
  remux_feed(r, pkt, capture_cb, NULL);
  ck_assert(!saw_pid(0x0012));

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_sdt_nit_ait_sent_when_configured) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  remux_t *r;
  unsigned char pkt[188];

  base_cfg(&cfg);
  cfg.sdt_mode = TABLE_OVERRIDE;
  snprintf(cfg.sdt_text, sizeof cfg.sdt_text, "Test Service");
  cfg.nit_mode = TABLE_OVERRIDE;
  snprintf(cfg.nit_text, sizeof cfg.nit_text, "Test Network");
  cfg.hbbtv_url = "http://example.invalid/app.html";
  cfg.hbbtv_org_id = 1;
  cfg.hbbtv_app_id = 2;

  r = remux_new(&cfg, psi);
  ck_assert_ptr_nonnull(r);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x1F; /* pid 0x1FFF: null packet, not carried in the ES map */
  pkt[2] = 0xFF;
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, pkt, capture_cb, NULL);

  ck_assert(saw_pid(0x0000)); /* PAT */
  ck_assert(saw_pid(0x1000)); /* PMT */
  ck_assert(saw_pid(0x0011)); /* SDT */
  ck_assert(saw_pid(0x0010)); /* NIT */
  ck_assert(saw_pid(0x0020)); /* AIT */

  remux_free(r);
  psi_free(psi);
}
END_TEST

static Suite *remux_suite(void) {
  Suite *s = suite_create("remux");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, remux_forwards_mapped_es_and_sends_pat_pmt_on_first_feed);
  tcase_add_test(tc, remux_drops_unrecognized_pid_and_does_not_resend_psi_immediately);
  tcase_add_test(tc, remux_eit_passthrough_respects_strip_eit);
  tcase_add_test(tc, remux_sdt_nit_ait_sent_when_configured);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(remux_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
