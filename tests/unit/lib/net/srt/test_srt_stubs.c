/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/net/srt/srtsink.h"
#include "lib/net/srt/srtsrc.h"

START_TEST(sink_stub_fails_cleanly) {
  srtsink_cfg_t cfg;
  srtsink_status_t st;
  unsigned char buf[188] = {0};
  int dummy = 0;
  srtsink_t *r = (srtsink_t *)&dummy;
  metrics_writer_t w;
  memset(&cfg, 0, sizeof cfg);
  memset(&st, 0xFF, sizeof st);
  ck_assert_ptr_null(srtsink_open(&cfg));
  srtsink_service(r, &st);
  ck_assert_int_eq(st.connected, 0);
  ck_assert_int_eq(srtsink_write(r, buf, sizeof buf), -1);
  srtsink_put_queue_metrics(&w, &dummy);
  srtsink_close(NULL);
}
END_TEST

START_TEST(source_stub_fails_cleanly) {
  srtsrc_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_ptr_null(srtsrc_open(&cfg));
  ck_assert_int_eq(srtsrc_fd(NULL), -1);
  srtsrc_close(NULL);
}
END_TEST

static Suite *srt_stub_suite(void) {
  Suite *s = suite_create("srt_stubs");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, sink_stub_fails_cleanly);
  tcase_add_test(tc, source_stub_fails_cleanly);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(srt_stub_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
