/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../log_capture.h"
#include "dipidescramble/pmt_select.h"
#include "lib/mux/esbuild/pmtbuild.h"
#include "lib/mux/psi_build.h"

#define PMT_A 0x0100
#define PMT_B 0x0200

typedef struct {
  char path[64];
  tssrc_t *src;
} fx_t;

static void put_section_packet(FILE *f, unsigned pid, const unsigned char *sec, size_t len) {
  unsigned char pkt[188];

  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, sec, len);
  memset(pkt + 5 + len, 0xFF, 188 - 5 - len);
  ck_assert_uint_eq(fwrite(pkt, 1, sizeof pkt, f), sizeof pkt);
}

static void fx_open(fx_t *fx, int programs) {
  unsigned char sec[256];
  size_t n;
  FILE *f;
  int fd;
  tssrc_cfg_t tc;
  psi_pat_entry_t progs[2];
  esbuild_es_t es = {0x0101, CODEC_H264};

  snprintf(fx->path, sizeof fx->path, "/tmp/dscr_pmtsel_XXXXXX");
  fd = mkstemp(fx->path);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "wb");
  ck_assert_ptr_nonnull(f);
  progs[0].program_number = 101;
  progs[0].pmt_pid = PMT_A;
  progs[1].program_number = 102;
  progs[1].pmt_pid = PMT_B;
  for (int rep = 0; rep < 4 && programs > 0; rep++) {
    n = programs == 1 ? psi_build_pat(0x1234, 0, 101, PMT_A, sec, sizeof sec) : psi_build_pat_multi(0x1234, 0, progs, 2, sec, sizeof sec);
    put_section_packet(f, 0x0000, sec, n);
    if (programs > 1) {
      n = esbuild_build_pmt(0, 101, &es, 1, sec, sizeof sec);
      put_section_packet(f, PMT_A, sec, n);
      n = esbuild_build_pmt(0, 102, &es, 1, sec, sizeof sec);
      put_section_packet(f, PMT_B, sec, n);
      n = psi_build_sdt(0, 0x1234, 5, 101, 0x01, "P", "One", sec, sizeof sec);
      put_section_packet(f, 0x0011, sec, n);
      n = psi_build_sdt(0, 0x1234, 5, 102, 0x01, "P", "Two", sec, sizeof sec);
      put_section_packet(f, 0x0011, sec, n);
    }
  }
  fclose(f);
  memset(&tc, 0, sizeof tc);
  tc.kind = TSSRC_FILE;
  tc.file_path = fx->path;
  fx->src = tssrc_open(&tc, NULL);
  ck_assert_ptr_nonnull(fx->src);
}

static void fx_close(fx_t *fx) {
  tssrc_close(fx->src);
  unlink(fx->path);
}

START_TEST(single_program_source_proceeds_and_notes_an_ignored_p) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid = 9;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all = 9;
  char log[LOG_CAPTURE_BUF];

  fx_open(&fx, 1);
  memset(&cfg, 0, sizeof cfg);
  cfg.pmt_sel = PMT_SEL_PID;
  cfg.pmt_pid = 0x1234;
  log_capture_begin();
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 0);
  log_capture_end(log, sizeof log);
  ck_assert_uint_eq(pmt_pid, 0u);
  ck_assert_int_eq(n_all, 0);
  ck_assert_int_eq(log_count_of(log, "-p ignored"), 1);
  fx_close(&fx);
}
END_TEST

START_TEST(single_program_source_without_p_is_silent) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all;
  char log[LOG_CAPTURE_BUF];

  fx_open(&fx, 1);
  memset(&cfg, 0, sizeof cfg);
  log_capture_begin();
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 0);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(log_count_of(log, "-p ignored"), 0);
  fx_close(&fx);
}
END_TEST

START_TEST(mpts_without_p_aborts_and_lists_the_programs) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all;
  char log[LOG_CAPTURE_BUF];

  fx_open(&fx, 2);
  memset(&cfg, 0, sizeof cfg);
  log_capture_begin();
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 1);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(log_count_of(log, "MPTS source, pick one"), 1);
  ck_assert_int_eq(log_count_of(log, "2 program(s) available"), 1);
  fx_close(&fx);
}
END_TEST

START_TEST(mpts_with_a_matching_pid_selects_it) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid = 0;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all = 5;

  fx_open(&fx, 2);
  memset(&cfg, 0, sizeof cfg);
  cfg.pmt_sel = PMT_SEL_PID;
  cfg.pmt_pid = PMT_B;
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 0);
  ck_assert_uint_eq(pmt_pid, (unsigned)PMT_B);
  ck_assert_int_eq(n_all, 0);
  fx_close(&fx);
}
END_TEST

START_TEST(mpts_with_an_unknown_pid_aborts) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all;
  char log[LOG_CAPTURE_BUF];

  fx_open(&fx, 2);
  memset(&cfg, 0, sizeof cfg);
  cfg.pmt_sel = PMT_SEL_PID;
  cfg.pmt_pid = 0x0999;
  log_capture_begin();
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 1);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(log_count_of(log, "not found in this MPTS"), 1);
  fx_close(&fx);
}
END_TEST

START_TEST(mpts_with_all_collects_every_pmt_pid_for_ts_output) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid = 7;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all = 0;

  fx_open(&fx, 2);
  memset(&cfg, 0, sizeof cfg);
  cfg.pmt_sel = PMT_SEL_ALL;
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 0);
  ck_assert_uint_eq(pmt_pid, 0u);
  ck_assert_int_eq(n_all, 2);
  ck_assert((all[0] == PMT_A && all[1] == PMT_B) || (all[0] == PMT_B && all[1] == PMT_A));
  fx_close(&fx);
}
END_TEST

START_TEST(mpts_with_all_is_refused_for_mkv_and_rtmp_outputs) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all;
  char log[LOG_CAPTURE_BUF];

  fx_open(&fx, 2);
  memset(&cfg, 0, sizeof cfg);
  cfg.pmt_sel = PMT_SEL_ALL;
  cfg.format = FMT_MKV;
  log_capture_begin();
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 1);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(log_count_of(log, "can't hold multiple programs"), 1);
  fx_close(&fx);

  fx_open(&fx, 2);
  memset(&cfg, 0, sizeof cfg);
  cfg.pmt_sel = PMT_SEL_ALL;
  cfg.n_out = 2;
  cfg.out[0].kind = OUT_FILE;
  cfg.out[1].kind = OUT_RTMPS;
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 1);
  fx_close(&fx);

  fx_open(&fx, 2);
  memset(&cfg, 0, sizeof cfg);
  cfg.pmt_sel = PMT_SEL_ALL;
  cfg.n_out = 1;
  cfg.out[0].kind = OUT_RTMP;
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 1);
  fx_close(&fx);
}
END_TEST

START_TEST(a_source_without_any_pat_gives_up) {
  fx_t fx;
  config_t cfg;
  unsigned pmt_pid;
  unsigned all[PSI_MAX_PROGRAMS];
  int n_all;
  char log[LOG_CAPTURE_BUF];

  fx_open(&fx, 0);
  memset(&cfg, 0, sizeof cfg);
  log_capture_begin();
  ck_assert_int_eq(dscr_resolve_pmt_selection(&cfg, fx.src, &pmt_pid, all, &n_all), 1);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(log_count_of(log, "no PAT received"), 1);
  fx_close(&fx);
}
END_TEST

static Suite *pmt_select_suite(void) {
  Suite *s = suite_create("dipidescramble_pmt_select");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, single_program_source_proceeds_and_notes_an_ignored_p);
  tcase_add_test(tc, single_program_source_without_p_is_silent);
  tcase_add_test(tc, mpts_without_p_aborts_and_lists_the_programs);
  tcase_add_test(tc, mpts_with_a_matching_pid_selects_it);
  tcase_add_test(tc, mpts_with_an_unknown_pid_aborts);
  tcase_add_test(tc, mpts_with_all_collects_every_pmt_pid_for_ts_output);
  tcase_add_test(tc, mpts_with_all_is_refused_for_mkv_and_rtmp_outputs);
  tcase_add_test(tc, a_source_without_any_pat_gives_up);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pmt_select_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
