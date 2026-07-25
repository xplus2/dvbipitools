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
  cfg.tsid = 1;
  cfg.onid = 2;
  cfg.sid = 101;
  cfg.stream_type = 0x0F;
  cfg.network_name = "Test Network";
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  g_count = 0;
  tspacketizer_feed(t, 0, g_frame, sizeof g_frame, capture_cb, NULL);

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
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.network_name = ""; /* no NIT */
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  g_count = 0;
  tspacketizer_feed(t, 0, g_frame, sizeof g_frame, capture_cb, NULL);

  ck_assert(!saw_pid(0x0010));
  ck_assert(saw_pid(0x0000));

  tspacketizer_free(t);
}
END_TEST

START_TEST(tspacketizer_second_feed_shortly_after_only_sends_audio) {
  tspacketizer_cfg_t cfg;
  tspacketizer_t *t;
  memset(&cfg, 0, sizeof cfg);
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.network_name = "";
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  tspacketizer_feed(t, 0, g_frame, sizeof g_frame, capture_cb, NULL); /* prime all timers */

  g_count = 0;
  tspacketizer_feed(t, 100, g_frame, sizeof g_frame, capture_cb, NULL); /* well under any interval */

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
  cfg.tsid = 1;
  cfg.sid = 101;
  cfg.network_name = "";
  cfg.service_name = "Test Service";
  t = tspacketizer_new(&cfg);

  tspacketizer_feed(t, 0, g_frame, sizeof g_frame, capture_cb, NULL);
  tspacketizer_set_metadata(t, "Some Artist", "Some Title");

  g_count = 0;
  tspacketizer_feed(t, 100, g_frame, sizeof g_frame, capture_cb, NULL); /* EIT timer not due, but metadata changed */

  ck_assert(saw_pid(0x0012));
  ck_assert(!saw_pid(0x0000)); /* PAT/PMT still not due */

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
