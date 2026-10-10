/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "../metrics_sink.h"
#include "../run_helper.h"
#include "dipisds/announce.h"
#include "lib/sys/signal.h"

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
  ck_assert_uint_eq(st.version, 1u);
  ck_assert_ptr_nonnull(memmem(st.broadcast_doc, st.broadcast_len, "Version=\"1\"", 11));
  ck_assert_uint_gt(st.broadcast_len, 0u);
  ck_assert_uint_gt(st.sp_len, 0u);
  ck_assert_ptr_nonnull(memmem(st.broadcast_doc, st.broadcast_len, "example.org", strlen("example.org")));
  ck_assert_ptr_nonnull(memmem(st.sp_doc, st.sp_len, "My Headend", strlen("My Headend")));

  state_free(&st);
  unlink(path);
}
END_TEST

START_TEST(state_reload_bumps_version_in_docs_and_keeps_state_on_failure) {
  char path[160];
  config_t cfg;
  sds_state_t st;
  unsigned i;

  write_temp_file(path, ".csv", "Alpha,rtp://239.1.1.1:5000\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = path;
  cfg.provider = "example.org";
  cfg.offering = "My Headend";
  memcpy(cfg.lang, "deu", 3);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.255.0.1");
  cfg.mcast_port = 3937;

  ck_assert_int_eq(state_load(&cfg, &st), 0);
  write_temp_file(path, ".csv", "Alpha,rtp://239.1.1.1:5000\nGamma,rtp://239.1.1.2:5000\n");
  cfg.input_path = path;
  ck_assert_int_eq(state_reload(&cfg, &st), 0);
  ck_assert_uint_eq(st.version, 2u);
  ck_assert_int_eq(st.in.service_count, 2);
  ck_assert_ptr_nonnull(memmem(st.broadcast_doc, st.broadcast_len, "Version=\"2\"", 11));
  ck_assert_ptr_nonnull(memmem(st.sp_doc, st.sp_len, "Version=\"2\"", 11));

  cfg.input_path = "/nonexistent/services.csv";
  ck_assert_int_eq(state_reload(&cfg, &st), -1);
  ck_assert_uint_eq(st.version, 2u);
  ck_assert_int_eq(st.in.service_count, 2);

  cfg.input_path = path;
  st.version = 255;
  ck_assert_int_eq(state_reload(&cfg, &st), 0);
  ck_assert_uint_eq(st.version, 1u);
  for (i = 0; i < 3; i++) ck_assert_int_eq(state_reload(&cfg, &st), 0);
  ck_assert_uint_eq(st.version, 4u);

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

static void base_cfg(config_t *cfg, const char *input_path) {
  memset(cfg, 0, sizeof *cfg);
  cfg->input_path = input_path;
  cfg->provider = "example.org";
  cfg->offering = "My Headend";
  memcpy(cfg->lang, "deu", 3);
  cfg->family = AF_INET;
  strcpy(cfg->mcast_group, "239.255.0.1");
  cfg->mcast_port = 3937;
}

typedef enum { SIDE_PACKAGES, SIDE_CELLS } side_file_t;

typedef struct {
  side_file_t side;
  const char *content;
  const char *msg;
} side_case_t;

static const side_case_t side_cases[] = {
  {SIDE_PACKAGES, NULL, "cannot open"},
  {SIDE_CELLS, NULL, "cannot open"},
  {SIDE_PACKAGES, "1,Bundle,eng,1,Channel One\n1,Again,eng,1,Channel One\nx,Bad,eng,1,Channel One\n", "bad package id"},
  {SIDE_PACKAGES, "1,Bundle,en,1,Channel One\n", "bad lang"},
  {SIDE_CELLS, "Paris East,FRA,1:IDF\n", "bad country code"},
  {SIDE_CELLS, "Paris East,FR\n", "no CA entries"},
  {SIDE_CELLS, "Paris East,FR,IDF\n", "bad CA entry"},
};

START_TEST(state_load_fails_on_unreadable_or_bad_side_files) {
  const side_case_t *c = &side_cases[_i];
  char csv_path[160];
  char side_path[160];
  char msg[4096];
  config_t cfg;
  sds_state_t st;
  int rc;

  write_temp_file(csv_path, ".csv", "Channel One,rtp://239.1.1.1:5000\n");
  base_cfg(&cfg, csv_path);
  if (c->content) {
    write_temp_file(side_path, ".csv", c->content);
  } else {
    snprintf(side_path, sizeof side_path, "/nonexistent/dvbipitools_side_%d.csv", _i);
  }
  if (c->side == SIDE_PACKAGES) cfg.packages_path = side_path;
  else cfg.cells_path = side_path;
  log_capture_begin();
  rc = state_load(&cfg, &st);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, -1);
  ck_assert_ptr_nonnull(strstr(msg, c->msg));
  unlink(csv_path);
  if (c->content) unlink(side_path);
}
END_TEST

#define BIG_LINES 256
#define BIG_NAME 100
#define BIG_BUF 524288
#define AMPS "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&"

typedef enum { BIG_BROADCAST, BIG_PACKAGE, BIG_CELL, BIG_RMSFUS } big_kind_t;

static const char *const big_msgs[] = {
  "SD&S document too large",
  "Package Discovery document too large",
  "Regionalisation Discovery document too large",
  "RMS-FUS Discovery document too large",
};

static void fill_services(char *buf, size_t cap, int count) {
  size_t n = 0;

  for (int i = 0; i < count; i++)
    n += (size_t)snprintf(buf + n, cap - n, "%0*d,rtp://239.1.1.1:5000,1,2,%d\n", BIG_NAME, i, 100 + i);
}

static void fill_packages(char *buf, size_t cap, int count) {
  size_t n = 0;

  for (int i = 0; i < count; i++) {
    n += (size_t)snprintf(buf + n, cap - n, "%d,B%d%s%s,eng,1,", i + 1, i, AMPS, AMPS);
    for (int k = 0; k < 7; k++) n += (size_t)snprintf(buf + n, cap - n, "%s%0*d", k ? "|" : "", BIG_NAME, k);
    n += (size_t)snprintf(buf + n, cap - n, "\n");
  }
}

static void fill_cells(char *buf, size_t cap, int count) {
  size_t n = 0;

  for (int i = 0; i < count; i++) {
    n += (size_t)snprintf(buf + n, cap - n, "Cell %d%s,FR", i, AMPS);
    for (int k = 0; k < 8; k++) n += (size_t)snprintf(buf + n, cap - n, ",%d:%s", k + 1, AMPS);
    n += (size_t)snprintf(buf + n, cap - n, "\n");
  }
}

START_TEST(state_load_reports_documents_over_the_size_cap) {
  big_kind_t kind = (big_kind_t)_i;
  char csv_path[160];
  char side_path[160] = "";
  char msg[4096];
  char *buf = malloc(BIG_BUF);
  char *rms_logo = NULL;
  config_t cfg;
  sds_state_t st;
  int rc;

  ck_assert_ptr_nonnull(buf);
  fill_services(buf, BIG_BUF, kind == BIG_BROADCAST ? BIG_LINES : 64);
  write_temp_file(csv_path, ".csv", buf);
  base_cfg(&cfg, csv_path);
  if (kind == BIG_PACKAGE) {
    fill_packages(buf, BIG_BUF, 64);
    write_temp_file(side_path, ".csv", buf);
    cfg.packages_path = side_path;
  } else if (kind == BIG_CELL) {
    fill_cells(buf, BIG_BUF, 64);
    write_temp_file(side_path, ".csv", buf);
    cfg.cells_path = side_path;
  } else if (kind == BIG_RMSFUS) {
    rms_logo = malloc(70000);
    ck_assert_ptr_nonnull(rms_logo);
    memset(rms_logo, 'a', 69999);
    rms_logo[69999] = '\0';
    cfg.rms_enabled = 1;
    cfg.rms_name = "My RMS";
    memcpy(cfg.rms_lang, "deu", 3);
    cfg.rms_location = "https://rms.example/";
    cfg.rms_logo = rms_logo;
  }
  log_capture_begin();
  rc = state_load(&cfg, &st);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, -1);
  ck_assert_ptr_nonnull(strstr(msg, big_msgs[kind]));
  unlink(csv_path);
  if (side_path[0]) unlink(side_path);
  free(rms_logo);
  free(buf);
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

typedef struct {
  const char *initial;
  const char *suffix;
  const char *rewrite;
  unsigned rewrite_ms;
  unsigned stop_ms;
  const char *reload_msg;
  unsigned services;
  unsigned generated;
  unsigned doc_errors;
  unsigned cycles;
} announce_case_t;

static const announce_case_t announce_cases[] = {
  {"Channel One,rtp://239.1.1.1:5000\n", ".csv", "Channel One,rtp://239.1.1.1:5000\nChannel Two,rtp://239.1.1.2:5000\n", 200, 1250, "reloaded", 2, 2, 0, 2},
  {"Channel One,rtp://239.1.1.1:5000\n", ".csv", "garbage line without fields\n", 200, 1250, "reload failed, keeping previous input", 1, 1, 1, 2},
};

START_TEST(announce_run_reloads_on_sighup_and_keeps_the_old_input_on_failure) {
  const announce_case_t *c = &announce_cases[_i];
  char path[160];
  char group[32];
  char msg[8192];
  char want[256];
  run_helper_t h;
  pthread_t th;
  config_t cfg;
  sink_t ms;
  seen_t seen;
  uint64_t v = 0;
  int rc;

  write_temp_file(path, c->suffix, c->initial);
  base_cfg(&cfg, path);
  run_helper_group(group, sizeof group, 77);
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.interval_s = 1;
  h.path = path;
  h.rewrite = c->rewrite;
  h.rewrite_ms = c->rewrite_ms;
  h.stop_ms = c->stop_ms;
  sink_open(&ms, METRICS_COMPONENT_SDS, "sds1", 0.1);
  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, run_helper_thread, &h), 0);
  rc = announce_run(&cfg, &ms.mx);
  pthread_join(th, NULL);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, "announcing 1 service on"));
  if (c->reload_msg) ck_assert_ptr_nonnull(strstr(msg, c->reload_msg));
  snprintf(want, sizeof want, "stopped after %u cycle", c->cycles);
  ck_assert_ptr_nonnull(strstr(msg, want));
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_SERVICES, &v), 1);
  ck_assert_uint_eq(v, c->services);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_DOCUMENTS_GENERATED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, c->generated);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_DOCUMENT_ERRORS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, c->doc_errors);
  sink_close(&ms);
  unlink(path);
}
END_TEST

#define HUGE_XML_LEN 6000000u

typedef enum { EMIT_SERVICES, EMIT_RAW_XML, EMIT_SEND_FAILURE } emit_kind_t;

typedef struct {
  emit_kind_t kind;
  unsigned providers;
  unsigned services;
  unsigned sent;
  unsigned send_errors;
} emit_case_t;

static const emit_case_t emit_cases[] = {
  {EMIT_SERVICES, 1, 1, 1, 0},
  {EMIT_RAW_XML, 0, 0, 1, 0},
  {EMIT_SEND_FAILURE, 0, 0, 0, 1},
};

START_TEST(announce_run_exports_provider_service_and_announcement_metrics) {
  const emit_case_t *c = &emit_cases[_i];
  char path[160];
  char msg[8192];
  run_helper_t h;
  pthread_t th;
  config_t cfg;
  sink_t ms;
  seen_t seen;
  uint64_t v = 0;
  double before = (double)time(NULL);
  int rc;

  if (c->kind == EMIT_SERVICES) {
    write_temp_file(path, ".csv", "Channel One,rtp://239.1.1.1:5000\n");
  } else {
    char *big = malloc(HUGE_XML_LEN + 1);

    ck_assert_ptr_nonnull(big);
    if (c->kind == EMIT_RAW_XML) {
      snprintf(big, HUGE_XML_LEN, "<BroadcastDiscovery>raw</BroadcastDiscovery>\n");
    } else {
      memset(big, ' ', HUGE_XML_LEN);
      memcpy(big, "<BroadcastDiscovery>", 20);
      big[HUGE_XML_LEN] = '\0';
    }
    write_temp_file(path, ".xml", big);
    free(big);
  }
  base_cfg(&cfg, path);
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 78);
  cfg.interval_s = 1;
  h.path = path;
  h.rewrite = NULL;
  h.rewrite_ms = 0;
  h.stop_ms = 300;
  sink_open(&ms, METRICS_COMPONENT_SDS, "sds1", 0.1);
  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, run_helper_thread, &h), 0);
  rc = announce_run(&cfg, &ms.mx);
  pthread_join(th, NULL);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_SERVICE_PROVIDERS, &v), 1);
  ck_assert_uint_eq(v, c->providers);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_SERVICES, &v), 1);
  ck_assert_uint_eq(v, c->services);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_DOCUMENTS_GENERATED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_ANNOUNCEMENTS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, c->sent);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_ANNOUNCEMENT_ERRORS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, c->send_errors);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_SDS_LAST_SUCCESS_TIME_SECONDS, &v), 1);
  if (c->sent) ck_assert_uint_ge(v, (uint64_t)before);
  else ck_assert_uint_eq(v, 0u);
  sink_close(&ms);
  unlink(path);
}
END_TEST

static Suite *announce_suite(void) {
  Suite *s = suite_create("dipisds_announce");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, state_load_builds_broadcast_and_sp_docs_for_service_input);
  tcase_add_test(tc, state_reload_bumps_version_in_docs_and_keeps_state_on_failure);
  tcase_add_test(tc, state_load_leaves_docs_unset_for_raw_xml_input);
  tcase_add_test(tc, state_load_applies_ret_and_fcc_to_broadcast_doc);
  tcase_add_test(tc, state_load_builds_package_doc_and_lists_it_in_sp_doc);
  tcase_add_test(tc, state_load_builds_cell_doc);
  tcase_add_test(tc, state_load_builds_rmsfus_doc_for_rms);
  tcase_add_test(tc, state_load_builds_rmsfus_doc_for_fus);
  tcase_add_test(tc, state_load_rejects_bad_packages_file);
  tcase_add_loop_test(tc, state_load_fails_on_unreadable_or_bad_side_files, 0, (int)(sizeof side_cases / sizeof side_cases[0]));
  tcase_add_loop_test(tc, state_load_reports_documents_over_the_size_cap, 0, BIG_RMSFUS + 1);
  tcase_add_test(tc, state_load_rejects_missing_input);
  tcase_add_loop_test(tc, announce_run_exports_provider_service_and_announcement_metrics, 0, (int)(sizeof emit_cases / sizeof emit_cases[0]));
  tcase_add_loop_test(tc, announce_run_reloads_on_sighup_and_keeps_the_old_input_on_failure, 0, (int)(sizeof announce_cases / sizeof announce_cases[0]));
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
