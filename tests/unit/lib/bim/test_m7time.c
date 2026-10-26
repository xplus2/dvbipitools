/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/codec.h"
#include "lib/bim/codec/codec_int.h"

static const char *const tp_ok[] = {
  "", "2030", "-2030", "2030-12", "2030-12-31", "T12", "T12:30", "T12:30:45", "T12:30:45:3F9", "T12:30:45:3.99F9", "2030-12-31T12:30:45:3.99F9+01:00",
  "-2030-01-01T00:00:00:1.00F5-15:45", "F7", "+16:00", "2030-12-31T23:59:59F1+05:30", "0-01-01T00",
};

static const char *const tp_bad[] = {
  "x", "2030-", "2030-1", "2030-13", "2030-00", "2030-12-32", "2030-12-00", "2030-12-31x", "2030-12x", "T", "T1", "T24", "T12:60", "T12:30:60", "T12:30:45:3.99",
  "T12:30:45:3.100F9", "T12:30:45:3.1F9", "T12:30:45:0F0", "T12:30:45:99F9", "T12:30:45:3:", "T12:30:45.5F9", "2030F", "2030Fx", "2030+01:00x", "2030+17:00",
  "2030-16:00", "2030+01:20", "2030+01:60", "2030T12:30:45:3.99F9+01:01", "T12:30:45:xF9", "1x", "99999999999999999999999", "T12:30-00:30",
};

static const char *const du_ok[] = {
  "P", "-P", "P1D", "PT", "PT1H", "PT1M", "PT1S", "PT1N", "P1DT23H", "P1DT23H59M59S", "PT12H30M", "PT30M", "PT45S", "PT1H30M45S5N", "PT99f", "PT1H01f",
  "P1F", "P1DT1H59M59S3N07f9F", "P1DT1H+01:00Z", "-P5DT1H-15:45Z", "P3F+16:30Z", "PT10S", "PT90M", "PT75S", "P99D", "PT100H", "PT0H5S",
};

static const char *const du_bad[] = {
  "", "x", "1D", "P1X", "PT1H5", "P1DT24H", "P1DT1H60M", "P1DT1H1M60S", "PT99fX", "PT100f", "PTf", "P1D+33:00Z", "P1D-32:00Z", "PT1S0N0F",
  "PT1M3S5N2F", "P1F5", "PT2S9N0F", "99999999999999999999D", "P1DT1H+01:20Z", "P-1D", "P1F1F",
};

START_TEST(m7time_timepoint_parse_and_render) {
  for (size_t i = 0; i < sizeof tp_ok / sizeof *tp_ok; i++) {
    m7_timepoint_t t;
    char out[96];
    ck_assert_msg(!m7_timepoint_parse(tp_ok[i], &t), "parse %s", tp_ok[i]);
    ck_assert_int_eq(m7_timepoint_render(&t, out, sizeof out), 0);
    ck_assert_int_eq(m7_timepoint_render(&t, out, 2) != 0 || strlen(out) < 2, 1);
  }
  for (size_t i = 0; i < sizeof tp_bad / sizeof *tp_bad; i++) {
    m7_timepoint_t t;
    bitwriter_t bw;
    int parsed = m7_timepoint_parse(tp_bad[i], &t);
    bitwriter_init(&bw);
    ck_assert_msg(m7_timepoint_encode(tp_bad[i], NULL, 0, &bw) == -1, "encode %s (parse rc %d)", tp_bad[i], parsed);
    bitwriter_free(&bw);
  }
}
END_TEST

START_TEST(m7time_duration_parse_and_render) {
  for (size_t i = 0; i < sizeof du_ok / sizeof *du_ok; i++) {
    m7_duration_t d;
    char out[96];
    ck_assert_msg(!m7_duration_parse(du_ok[i], &d), "parse %s", du_ok[i]);
    ck_assert_int_eq(m7_duration_render(&d, out, sizeof out), 0);
    (void)m7_duration_render(&d, out, 2);
  }
  for (size_t i = 0; i < sizeof du_bad / sizeof *du_bad; i++) {
    bitwriter_t bw;
    bitwriter_init(&bw);
    ck_assert_msg(m7_duration_encode(du_bad[i], NULL, 0, &bw) == -1, "encode %s", du_bad[i]);
    bitwriter_free(&bw);
  }
}
END_TEST

static int only_listed(const void *pattern, const char *v) {
  const char *list = pattern;
  char key[64];
  snprintf(key, sizeof key, "|%s|", v);
  return strstr(list, key) != NULL;
}

static void encode_all(int duration, const bim_simple_t *sp, unsigned compat) {
  const char *const *ok = duration ? du_ok : tp_ok;
  size_t n = duration ? sizeof du_ok / sizeof *du_ok : sizeof tp_ok / sizeof *tp_ok;
  for (size_t i = 0; i < n; i++) {
    bitwriter_t bw;
    bitwriter_init(&bw);
    (void)(duration ? m7_duration_encode(ok[i], sp, compat, &bw) : m7_timepoint_encode(ok[i], sp, compat, &bw));
    bitwriter_free(&bw);
  }
}

START_TEST(m7time_flags_follow_the_simple_type_patterns) {
  static const char all_tp[] = "|-1|1|1-01|1-01-01|T00|T00:00|T00:00:00|T00:00:00:1|T00:00:00:1.00|F1|+01:00|";
  static const char date_only[] = "|1|1-01|1-01-01|";
  static const char no_tz[] = "|-1|1|1-01|1-01-01|T00|T00:00|T00:00:00|T00:00:00:1|T00:00:00:1.00|F1|";
  static const char all_du[] = "|-P|P1D|PT|PT1H|PT1M|PT1S|PT1N|PT01f|P1F|P+01:00Z|";
  static const char hms_only[] = "|PT|PT1H|PT1M|PT1S|";
  static const void *const p_all_tp[] = {all_tp};
  static const void *const p_date[] = {date_only};
  static const void *const p_no_tz[] = {no_tz};
  static const void *const p_all_du[] = {all_du};
  static const void *const p_hms[] = {hms_only};
  bim_simple_t base = {0};
  bim_simple_t s = {0};
  unsigned f;
  s.match = only_listed;
  s.patterns = p_date;
  s.npatterns = 1;
  f = m7_timepoint_flags(&s, BIM_COMPAT_M7_FLAGS);
  ck_assert_uint_ne(f, 0);
  ck_assert_uint_eq(M7_HAS(f, M7T_TIME), 0);
  ck_assert_uint_eq(M7_HAS(m7_timepoint_flags(&s, 0), M7T_TIME), 1);
  encode_all(0, &s, BIM_COMPAT_M7_FLAGS);
  s.patterns = p_no_tz;
  encode_all(0, &s, BIM_COMPAT_M7_FLAGS);
  s.patterns = p_all_tp;
  base.match = only_listed;
  base.patterns = p_date;
  base.npatterns = 1;
  s.base = &base;
  encode_all(0, &s, BIM_COMPAT_M7_FLAGS);
  s.base = NULL;
  encode_all(0, &s, BIM_COMPAT_M7_FLAGS);
  s.patterns = p_hms;
  f = m7_duration_flags(&s, BIM_COMPAT_M7_FLAGS);
  ck_assert_uint_eq(M7_HAS(f, M7D_D), 0);
  ck_assert_uint_eq(M7_HAS(m7_duration_flags(&s, 0), M7D_D), 1);
  encode_all(1, &s, BIM_COMPAT_M7_FLAGS);
  s.patterns = p_all_du;
  encode_all(1, &s, BIM_COMPAT_M7_FLAGS);
  encode_all(1, NULL, BIM_COMPAT_M7_FLAGS);
  encode_all(0, NULL, BIM_COMPAT_M7_FLAGS);
  encode_all(0, NULL, 0);
  encode_all(1, NULL, 0);
}
END_TEST

START_TEST(m7time_encode_writer_failures) {
  for (int duration = 0; duration < 2; duration++) {
    const char *const *ok = duration ? du_ok : tp_ok;
    size_t n = duration ? sizeof du_ok / sizeof *du_ok : sizeof tp_ok / sizeof *tp_ok;
    for (size_t i = 0; i < n; i++)
      for (unsigned compat = 0; compat < 2; compat++)
        for (int pre = 0; pre < 70; pre++) {
          bitwriter_t bw;
          bitwriter_init(&bw);
          for (int k = 0; k < pre; k++) ck_assert_int_eq(bitwriter_put(&bw, 0xA5, 8), 0);
          alloc_fail_at(0);
          (void)(duration ? m7_duration_encode(ok[i], NULL, compat ? BIM_COMPAT_M7_FLAGS : 0, &bw) : m7_timepoint_encode(ok[i], NULL, compat ? BIM_COMPAT_M7_FLAGS : 0, &bw));
          alloc_fail_end();
          bitwriter_free(&bw);
        }
  }
}
END_TEST

static Suite *m7time_suite(void) {
  Suite *s = suite_create("bim_m7time");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, m7time_timepoint_parse_and_render);
  tcase_add_test(tc, m7time_duration_parse_and_render);
  tcase_add_test(tc, m7time_flags_follow_the_simple_type_patterns);
  tcase_add_test(tc, m7time_encode_writer_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(m7time_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
