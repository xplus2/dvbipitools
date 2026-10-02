/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/mux/tspacketizer.h"

#define MAX_SEEN 64

static unsigned g_pids[MAX_SEEN];
static int g_pusi[MAX_SEEN];
static int g_count;

static void capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if (g_count < MAX_SEEN) {
    g_pids[g_count] = (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
    g_pusi[g_count] = (pkt[1] & 0x40) ? 1 : 0;
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

static unsigned char g_frame[8] = {1, 2, 3, 4, 5, 6, 7, 8};

START_TEST(tspacketizer_first_feed_emits_all_tables_and_audio) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;
  memset(&cfg, 0, sizeof cfg);
  cfg.standalone = 1;
  cfg.tsid = 1;
  cfg.onid = 2;
  cfg.sid = 101;
  cfg.stream_type = 0x0F;
  cfg.network_name = "Test Network";
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  g_count = 0;
  tspacketizer_feed(t, 0, 0.0, g_frame, sizeof g_frame, capture_cb, NULL);

  ck_assert(saw_pid(0x0000)); /* PAT */
  ck_assert(saw_pid(0x0100)); /* PMT */
  ck_assert(saw_pid(0x0011)); /* SDT */
  ck_assert(saw_pid(0x0010)); /* NIT (network_name set) */
  ck_assert(saw_pid(0x0012)); /* EIT */
  ck_assert(saw_pid(0x0101)); /* audio PES */

  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_omits_nit_when_no_network_name) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;
  memset(&cfg, 0, sizeof cfg);
  cfg.standalone = 1;
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.network_name = ""; /* no NIT */
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  g_count = 0;
  tspacketizer_feed(t, 0, 0.0, g_frame, sizeof g_frame, capture_cb, NULL);

  ck_assert(!saw_pid(0x0010));
  ck_assert(saw_pid(0x0000));

  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_second_feed_shortly_after_only_sends_audio) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;
  memset(&cfg, 0, sizeof cfg);
  cfg.standalone = 1;
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.network_name = "";
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  tspacketizer_feed(t, 0, 0.0, g_frame, sizeof g_frame, capture_cb, NULL); /* prime all timers */

  g_count = 0;
  tspacketizer_feed(t, 100, 0.0, g_frame, sizeof g_frame, capture_cb, NULL); /* well under any interval */

  ck_assert(!saw_pid(0x0000));
  ck_assert(!saw_pid(0x0100));
  ck_assert(!saw_pid(0x0011));
  ck_assert(!saw_pid(0x0012));
  ck_assert(saw_pid(0x0101)); /* audio is sent every feed */

  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_set_metadata_forces_eit_resend) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;
  memset(&cfg, 0, sizeof cfg);
  cfg.standalone = 1;
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.network_name = "";
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  tspacketizer_feed(t, 0, 0.0, g_frame, sizeof g_frame, capture_cb, NULL);
  tspacketizer_set_metadata(t, "Some Artist", "Some Title");

  g_count = 0;
  tspacketizer_feed(t, 100, 0.0, g_frame, sizeof g_frame, capture_cb, NULL); /* EIT timer not due, but metadata changed */

  ck_assert(saw_pid(0x0012));
  ck_assert(!saw_pid(0x0000)); /* PAT/PMT still not due */

  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_non_standalone_emits_only_pmt_and_audio) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;
  memset(&cfg, 0, sizeof cfg);
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.network_name = "Test Network";
  cfg.service_name = "Test Service";
  cfg.pmt_pid = 0x0110;
  cfg.audio_pid = 0x0190;
  cfg.standalone = 0;
  t = tspacketizer_new(&cfg);

  g_count = 0;
  tspacketizer_feed(t, 0, 0.0, g_frame, sizeof g_frame, capture_cb, NULL);

  ck_assert(!saw_pid(0x0000)); /* no PAT */
  ck_assert(!saw_pid(0x0001)); /* no CAT */
  ck_assert(!saw_pid(0x0010)); /* no NIT */
  ck_assert(!saw_pid(0x0011)); /* no SDT */
  ck_assert(!saw_pid(0x0012)); /* no EIT */
  ck_assert(saw_pid(0x0110));  /* PMT, on the configured pid */
  ck_assert(saw_pid(0x0190));  /* audio PES, on the configured pid */

  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_get_sdt_info_and_build_eit_return_pullable_data) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;
  unsigned char sec[4096];
  psi_sdt_entry_t info;
  size_t n;

  memset(&cfg, 0, sizeof cfg);
  cfg.tsid = 1;
  cfg.onid = 2;
  cfg.sid = 101;
  cfg.network_name = "";
  cfg.service_name = "Test Service";
  cfg.standalone = 0;
  t = tspacketizer_new(&cfg);

  ck_assert_int_eq(tspacketizer_get_sdt_info(t, &info), 0);
  ck_assert_uint_eq(info.service_id, 101u);
  ck_assert_str_eq(info.service_name, "Test Service");

  ck_assert_int_eq(tspacketizer_eit_pending(t), 0);
  n = tspacketizer_build_eit(t, sec, sizeof sec);
  ck_assert_uint_gt(n, 0u);

  tspacketizer_set_metadata(t, "Some Artist", "Some Title");
  ck_assert_int_eq(tspacketizer_eit_pending(t), 1);
  n = tspacketizer_build_eit(t, sec, sizeof sec);
  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(tspacketizer_eit_pending(t), 0); /* cleared by build */

  tspacketizer_free(t);
}
END_TEST

#define CAP_MAX 256
#define FAKE_VENDORS 2

static unsigned char g_pkts[CAP_MAX][188];
static int g_npkts;

static void store_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if (g_npkts < CAP_MAX) memcpy(g_pkts[g_npkts], pkt, 188);
  g_npkts++;
}

static unsigned pkt_pid(const unsigned char *pkt) { return (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2]; }

static int count_pid(unsigned pid) {
  int n = 0;

  for (int i = 0; i < g_npkts && i < CAP_MAX; i++)
    if (pkt_pid(g_pkts[i]) == pid) n++;
  return n;
}

static const unsigned char *nth_pid(unsigned pid, int nth) {
  for (int i = 0; i < g_npkts && i < CAP_MAX; i++)
    if (pkt_pid(g_pkts[i]) == pid && nth-- == 0) return g_pkts[i];
  return NULL;
}

struct cas {
  unsigned ecm_pid[FAKE_VENDORS];
  unsigned emm_pid[FAKE_VENDORS];
  int ecm_ready[FAKE_VENDORS];
  int emm_queued[FAKE_VENDORS];
  double ecm_now[FAKE_VENDORS];
  unsigned scramble_calls;
  unsigned scramble_pid;
  double scramble_now;
};

static const unsigned char fake_ca_desc[6] = {0x09, 0x04, 0x4A, 0x75, 0xE0, 0x20};

size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap) {
  (void)c;
  if (cap < sizeof fake_ca_desc) return 0;
  memcpy(out, fake_ca_desc, sizeof fake_ca_desc);
  return sizeof fake_ca_desc;
}

size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap) {
  (void)c;
  if (cap < 12) return 0;
  memset(out, 0, 12);
  out[0] = 0x01;
  return 12;
}

size_t cas_vendor_count(cas_t *c) {
  (void)c;
  return FAKE_VENDORS;
}

unsigned cas_vendor_ecm_pid(cas_t *c, size_t idx) { return c->ecm_pid[idx]; }
unsigned cas_vendor_emm_pid(cas_t *c, size_t idx) { return c->emm_pid[idx]; }

int cas_vendor_ecm_due(cas_t *c, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len) {
  if (!c->ecm_ready[idx] || cap < 8) return -1;
  c->ecm_now[idx] = now;
  memset(out, 0, 8);
  out[0] = 0x80;
  out[7] = (unsigned char)idx;
  *out_len = 8;
  return 0;
}

int cas_vendor_next_emm(cas_t *c, size_t idx, unsigned char *out, size_t cap, size_t *out_len) {
  if (c->emm_queued[idx] <= 0 || cap < 8) return -1;
  c->emm_queued[idx]--;
  memset(out, 0, 8);
  out[0] = 0x82;
  out[7] = (unsigned char)idx;
  *out_len = 8;
  return 0;
}

void cas_scramble_packet(cas_t *c, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  c->scramble_calls++;
  c->scramble_pid = out_pid;
  c->scramble_now = now;
  emit(ctx, pkt188);
}

static void init_fake_cas(cas_t *c) {
  memset(c, 0, sizeof *c);
  c->ecm_pid[0] = 0x0020;
  c->emm_pid[0] = 0x0021;
  c->ecm_pid[1] = 0x0022;
  c->emm_pid[1] = 0x0023;
}

static tspacketizer_t *new_cas_packetizer(cas_t *c, int standalone) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;

  memset(&cfg, 0, sizeof cfg);
  cfg.standalone = standalone;
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.stream_type = 0x0F;
  cfg.network_name = "";
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);
  ck_assert_ptr_nonnull(t);
  tspacketizer_set_cas(t, c);
  return t;
}

START_TEST(tspacketizer_cas_first_feed_emits_cat_pmt_descriptor_ecm_emm_and_scrambles_audio) {
  cas_t c;
  tspacketizer_t *t;
  const unsigned char *pmt;
  int found = 0;

  init_fake_cas(&c);
  c.ecm_ready[0] = 1;
  c.ecm_ready[1] = 1;
  c.emm_queued[1] = 2;
  t = new_cas_packetizer(&c, 1);

  g_npkts = 0;
  tspacketizer_feed(t, 0, 5.5, g_frame, sizeof g_frame, store_cb, NULL);

  ck_assert_int_eq(count_pid(0x0001), 1);
  pmt = nth_pid(0x0100, 0);
  ck_assert_ptr_nonnull(pmt);
  for (int i = 5; i + (int)sizeof fake_ca_desc <= 188; i++)
    if (memcmp(pmt + i, fake_ca_desc, sizeof fake_ca_desc) == 0) found = 1;
  ck_assert_msg(found, "CA descriptor missing from PMT");
  ck_assert_int_eq(count_pid(0x0020), 1);
  ck_assert_int_eq(count_pid(0x0022), 1);
  ck_assert_int_eq(count_pid(0x0021), 0);
  ck_assert_int_eq(count_pid(0x0023), 2);
  ck_assert_uint_eq(nth_pid(0x0020, 0)[188 - 8], 0x80u);
  ck_assert_uint_eq(nth_pid(0x0023, 0)[188 - 8], 0x82u);
  ck_assert_uint_eq(nth_pid(0x0022, 0)[188 - 1], 1u);
  ck_assert_double_eq(c.ecm_now[0], 5.5);
  ck_assert_double_eq(c.ecm_now[1], 5.5);
  ck_assert_uint_gt(c.scramble_calls, 0u);
  ck_assert_uint_eq(c.scramble_pid, 0x0101u);
  ck_assert_double_eq(c.scramble_now, 5.5);
  ck_assert_int_ge(count_pid(0x0101), 1);
  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_cas_repeats_ecm_only_while_the_cas_reports_it_due) {
  cas_t c;
  tspacketizer_t *t;

  init_fake_cas(&c);
  c.ecm_ready[0] = 1;
  t = new_cas_packetizer(&c, 1);

  g_npkts = 0;
  tspacketizer_feed(t, 0, 1.0, g_frame, sizeof g_frame, store_cb, NULL);
  tspacketizer_feed(t, 100, 1.1, g_frame, sizeof g_frame, store_cb, NULL);
  ck_assert_int_eq(count_pid(0x0020), 2);
  ck_assert_uint_eq(nth_pid(0x0020, 0)[3] & 0x0F, 1u);
  ck_assert_uint_eq(nth_pid(0x0020, 1)[3] & 0x0F, 2u);

  c.ecm_ready[0] = 0;
  tspacketizer_feed(t, 200, 1.2, g_frame, sizeof g_frame, store_cb, NULL);
  ck_assert_int_eq(count_pid(0x0020), 2);
  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_cas_repeats_cat_and_pmt_on_their_interval_only) {
  cas_t c;
  tspacketizer_t *t;

  init_fake_cas(&c);
  t = new_cas_packetizer(&c, 1);

  g_npkts = 0;
  tspacketizer_feed(t, 0, 1.0, g_frame, sizeof g_frame, store_cb, NULL);
  tspacketizer_feed(t, 8999, 1.1, g_frame, sizeof g_frame, store_cb, NULL);
  ck_assert_int_eq(count_pid(0x0001), 1);
  ck_assert_int_eq(count_pid(0x0100), 1);
  tspacketizer_feed(t, 9000, 1.2, g_frame, sizeof g_frame, store_cb, NULL);
  ck_assert_int_eq(count_pid(0x0001), 2);
  ck_assert_int_eq(count_pid(0x0100), 2);
  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_cas_non_standalone_scrambles_but_sends_no_cat_ecm_or_emm) {
  cas_t c;
  tspacketizer_t *t;

  init_fake_cas(&c);
  c.ecm_ready[0] = 1;
  c.emm_queued[0] = 3;
  t = new_cas_packetizer(&c, 0);

  g_npkts = 0;
  tspacketizer_feed(t, 0, 2.5, g_frame, sizeof g_frame, store_cb, NULL);
  ck_assert_int_eq(count_pid(0x0001), 0);
  ck_assert_int_eq(count_pid(0x0020), 0);
  ck_assert_int_eq(count_pid(0x0021), 0);
  ck_assert_int_eq(count_pid(0x0100), 1);
  ck_assert_uint_gt(c.scramble_calls, 0u);
  ck_assert_double_eq(c.scramble_now, 2.5);
  tspacketizer_free(t);
}
END_TEST

static Suite *tspacketizer_suite(void) {
  Suite *s = suite_create("tspacketizer");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tspacketizer_first_feed_emits_all_tables_and_audio);
  tcase_add_test(tc, tspacketizer_omits_nit_when_no_network_name);
  tcase_add_test(tc, tspacketizer_second_feed_shortly_after_only_sends_audio);
  tcase_add_test(tc, tspacketizer_set_metadata_forces_eit_resend);
  tcase_add_test(tc, tspacketizer_non_standalone_emits_only_pmt_and_audio);
  tcase_add_test(tc, tspacketizer_get_sdt_info_and_build_eit_return_pullable_data);
  tcase_add_test(tc, tspacketizer_cas_first_feed_emits_cat_pmt_descriptor_ecm_emm_and_scrambles_audio);
  tcase_add_test(tc, tspacketizer_cas_repeats_ecm_only_while_the_cas_reports_it_due);
  tcase_add_test(tc, tspacketizer_cas_repeats_cat_and_pmt_on_their_interval_only);
  tcase_add_test(tc, tspacketizer_cas_non_standalone_scrambles_but_sends_no_cat_ecm_or_emm);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tspacketizer_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
