/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipisds/announce.h"

static void write_temp_file(char *path, const char *suffix, const char *content) {
  char tmpl[128];
  int fd;
  FILE *f;
  snprintf(tmpl, sizeof tmpl, "/tmp/dvbipitools_test_announce_XXXXXX%s", suffix);
  strcpy(path, tmpl);
  fd = mkstemps(path, (int)strlen(suffix));
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  fputs(content, f);
  fclose(f);
}

START_TEST(state_load_builds_broadcast_and_sp_docs_for_service_input) {
  char path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(path, ".csv", "Channel One,rtp://239.1.1.1:5000,1,2,101\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  ck_assert_int_eq(st.in.kind, INPUT_SERVICES);
  ck_assert_int_eq(st.in.service_count, 1);
  ck_assert_uint_gt(st.broadcast_len, 0u);
  ck_assert_uint_gt(st.sp_len, 0u);
  ck_assert_ptr_nonnull(memmem(st.broadcast_doc, st.broadcast_len, "example.org", strlen("example.org")));
  ck_assert_ptr_nonnull(memmem(st.sp_doc, st.sp_len, "My Headend", strlen("My Headend")));

  state_free(&st);
  unlink(path);
}
END_TEST

START_TEST(state_load_leaves_docs_unset_for_raw_xml_input) {
  char path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(path, ".xml", "<BroadcastDiscovery>raw</BroadcastDiscovery>\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = path;

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  ck_assert_int_eq(st.in.kind, INPUT_RAW_XML);
  ck_assert_ptr_null(st.broadcast_doc);
  ck_assert_ptr_null(st.sp_doc);
  ck_assert_uint_eq(st.broadcast_len, 0u);
  ck_assert_uint_eq(st.sp_len, 0u);

  state_free(&st);
  unlink(path);
}
END_TEST

START_TEST(state_load_applies_ret_and_fcc_to_broadcast_doc) {
  char path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(path, ".csv", "Channel One,rtp://239.1.1.1:5000,1,2,101\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;
  cfg.ret_enabled = 1;
  strcpy(cfg.ret_addr, "10.0.0.1");
  cfg.ret_port = 6000;
  cfg.ret_rtx_time = 2000;
  cfg.ret_rtx_pt = 99;

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  ck_assert_ptr_nonnull(memmem(st.broadcast_doc, st.broadcast_len, "RTPRetransmission", strlen("RTPRetransmission")));

  state_free(&st);
  unlink(path);
}
END_TEST

START_TEST(state_load_builds_package_doc_and_lists_it_in_sp_doc) {
  char csv_path[160], pkg_path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(csv_path, ".csv", "Channel One,rtp://239.1.1.1:5000,1,2,101\n");
  write_temp_file(pkg_path, ".csv", "1,Bundle,eng,1,Channel One\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = csv_path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;
  cfg.packages_path = pkg_path;

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  ck_assert_uint_gt(st.package_len, 0u);
  ck_assert_ptr_nonnull(memmem(st.package_doc, st.package_len, "<Package Id=\"1\"", strlen("<Package Id=\"1\"")));
  ck_assert_ptr_nonnull(memmem(st.package_doc, st.package_len, "<DVBTriplet OrigNetId=\"2\" TSId=\"1\" ServiceId=\"101\"/>",
                                strlen("<DVBTriplet OrigNetId=\"2\" TSId=\"1\" ServiceId=\"101\"/>")));
  ck_assert_ptr_nonnull(memmem(st.sp_doc, st.sp_len, "<PayloadId Id=\"5\"/>", strlen("<PayloadId Id=\"5\"/>")));

  state_free(&st);
  unlink(csv_path);
  unlink(pkg_path);
}
END_TEST

START_TEST(state_load_builds_cell_doc) {
  char csv_path[160], cells_path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(csv_path, ".csv", "Channel One,rtp://239.1.1.1:5000\n");
  write_temp_file(cells_path, ".csv", "Paris East,FR,1:IDF\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = csv_path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;
  cfg.cells_path = cells_path;

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  ck_assert_uint_gt(st.cell_len, 0u);
  ck_assert_ptr_nonnull(memmem(st.cell_doc, st.cell_len, "<Cell Id=\"Paris East\">", strlen("<Cell Id=\"Paris East\">")));
  ck_assert_ptr_nonnull(memmem(st.sp_doc, st.sp_len, "<PayloadId Id=\"7\"/>", strlen("<PayloadId Id=\"7\"/>")));

  state_free(&st);
  unlink(csv_path);
  unlink(cells_path);
}
END_TEST

START_TEST(state_load_builds_rmsfus_doc_for_rms) {
  char csv_path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(csv_path, ".csv", "Channel One,rtp://239.1.1.1:5000\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = csv_path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;
  cfg.rms_enabled = 1;
  cfg.rms_name = "My RMS";
  memcpy(cfg.rms_lang, "deu", 3);
  cfg.rms_location = "https://rms.example/";

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  ck_assert_uint_gt(st.rmsfus_len, 0u);
  ck_assert_ptr_nonnull(memmem(st.rmsfus_doc, st.rmsfus_len, "<RMSProvider RMSLocation=\"https://rms.example/\">",
                                strlen("<RMSProvider RMSLocation=\"https://rms.example/\">")));
  ck_assert_ptr_nonnull(memmem(st.sp_doc, st.sp_len, "<PayloadId Id=\"8\"/>", strlen("<PayloadId Id=\"8\"/>")));

  state_free(&st);
  unlink(csv_path);
}
END_TEST

START_TEST(state_load_builds_rmsfus_doc_for_fus) {
  char csv_path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(csv_path, ".csv", "Channel One,rtp://239.1.1.1:5000\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = csv_path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;
  cfg.fus_enabled = 1;
  cfg.fus_name = "My FUS";
  memcpy(cfg.fus_lang, "deu", 3);
  cfg.fus_id = 42;

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  ck_assert_uint_gt(st.rmsfus_len, 0u);
  ck_assert_ptr_nonnull(memmem(st.rmsfus_doc, st.rmsfus_len, "<FUSID>42</FUSID>", strlen("<FUSID>42</FUSID>")));

  state_free(&st);
  unlink(csv_path);
}
END_TEST

START_TEST(state_load_rejects_bad_packages_file) {
  char csv_path[160], pkg_path[160];
  config_t cfg;
  sds_state_t st;

  write_temp_file(csv_path, ".csv", "Channel One,rtp://239.1.1.1:5000\n");
  write_temp_file(pkg_path, ".csv", "not,enough,fields\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = csv_path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;
  cfg.packages_path = pkg_path;

  ck_assert_int_eq(state_load(&cfg, &st), -1);
  unlink(csv_path);
  unlink(pkg_path);
}
END_TEST

START_TEST(state_load_rejects_missing_input) {
  config_t cfg;
  sds_state_t st;
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = "/nonexistent/dvbipitools_test_announce.csv";
  ck_assert_int_eq(state_load(&cfg, &st), -1);
}
END_TEST

static Suite *announce_suite(void) {
  Suite *s = suite_create("dipisds_announce");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, state_load_builds_broadcast_and_sp_docs_for_service_input);
  tcase_add_test(tc, state_load_leaves_docs_unset_for_raw_xml_input);
  tcase_add_test(tc, state_load_applies_ret_and_fcc_to_broadcast_doc);
  tcase_add_test(tc, state_load_builds_package_doc_and_lists_it_in_sp_doc);
  tcase_add_test(tc, state_load_builds_cell_doc);
  tcase_add_test(tc, state_load_builds_rmsfus_doc_for_rms);
  tcase_add_test(tc, state_load_builds_rmsfus_doc_for_fus);
  tcase_add_test(tc, state_load_rejects_bad_packages_file);
  tcase_add_test(tc, state_load_rejects_missing_input);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(announce_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
