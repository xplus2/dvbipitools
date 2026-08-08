/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipixmltv/suggest.h"

START_TEST(suggest_map_classifies_exact_fuzzy_and_unmatched) {
  FILE *xmltv_f, *scan_f, *out_f;
  char *buf;
  size_t len;

  xmltv_f = tmpfile();
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n"
        "<channel id=\"channel1\"><display-name>Channel One</display-name></channel>\n"
        "<channel id=\"channel2\"><display-name>Channel 2</display-name></channel>\n"
        "<channel id=\"mystery\"><display-name>Mystery Channel</display-name></channel>\n"
        "</tv>\n",
        xmltv_f);
  rewind(xmltv_f);

  scan_f = tmpfile();
  fputs("Channel One,rtp://239.1.1.1:5000,1,2,101\nChannel 2 HD,rtp://239.1.1.2:5000,1,2,102\n", scan_f);
  rewind(scan_f);

  out_f = open_memstream(&buf, &len);
  ck_assert_int_eq(suggest_map(xmltv_f, scan_f, out_f), 0);
  fclose(out_f);
  fclose(xmltv_f);
  fclose(scan_f);

  ck_assert_ptr_nonnull(strstr(buf, "channel1,rtp://239.1.1.1:5000,1,2,101\n"));
  ck_assert_ptr_nonnull(strstr(buf, "# channel2 (Channel 2) -> closest: Channel 2 HD, rtp://239.1.1.2:5000,1,2,102\n"));
  ck_assert_ptr_nonnull(strstr(buf, "# UNMATCHED: mystery (Mystery Channel)\n"));

  free(buf);
}
END_TEST

static Suite *suggest_suite(void) {
  Suite *s = suite_create("suggest");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, suggest_map_classifies_exact_fuzzy_and_unmatched);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(suggest_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
