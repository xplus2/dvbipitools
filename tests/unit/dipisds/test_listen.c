/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "../run_helper.h"
#include "dipisds/listen.h"
#include "lib/helper/sds_xml.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/sys/signal.h"

static dvbstp_header_t make_header(unsigned payload_id, unsigned segment_id, unsigned version) {
  dvbstp_header_t h;
  memset(&h, 0, sizeof h);
  h.payload_id = payload_id;
  h.segment_id = segment_id;
  h.segment_version = version;
  return h;
}

START_TEST(already_seen_is_false_on_first_sighting) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t h = make_header(1, 2, 3);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 0);
  ck_assert_int_eq(count, 1);
}
END_TEST

START_TEST(already_seen_is_true_on_repeat) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t h = make_header(1, 2, 3);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 1);
  ck_assert_int_eq(count, 1);
}
END_TEST

START_TEST(already_seen_distinguishes_by_all_three_fields) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t a = make_header(1, 2, 3);
  dvbstp_header_t b = make_header(1, 2, 4); /* different version */
  dvbstp_header_t c = make_header(1, 9, 3); /* different segment_id */
  dvbstp_header_t d = make_header(9, 2, 3); /* different payload_id */
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &a), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &b), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &c), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &d), 0);
  ck_assert_int_eq(count, 4);
}
END_TEST

START_TEST(already_seen_stops_recording_past_the_cap) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  int i;
  for (i = 0; i < LISTEN_SEEN_MAX + 5; i++) {
    dvbstp_header_t h = make_header((unsigned)i, 0, 0);
    ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 0);
  }
  ck_assert_int_eq(count, LISTEN_SEEN_MAX);
}
END_TEST

#define DOC_CAP 8192

typedef struct {
  dvbstp_sender_t sd;
  unsigned char buf[2][DOC_CAP];
} sds_sender_t;

static void build_docs(sds_sender_t *sd) {
  sds_service_t svc[2];

  memset(svc, 0, sizeof svc);
  snprintf(svc[0].name, sizeof svc[0].name, "Svc A");
  snprintf(svc[0].address, sizeof svc[0].address, "239.1.1.1");
  svc[0].family = AF_INET;
  svc[0].port = 5000;
  svc[0].rtp = 1;
  svc[0].tsid = 1;
  svc[0].onid = 2;
  svc[0].sid = 101;
  svc[1] = svc[0];
  snprintf(svc[1].name, sizeof svc[1].name, "Svc B");
  snprintf(svc[1].address, sizeof svc[1].address, "239.1.1.2");
  svc[1].sid = 102;
  sd->sd.lens[0] = sds_build_broadcast("example.org", 1, svc, 2, NULL, NULL, NULL, sd->buf[0], DOC_CAP);
  sd->sd.lens[1] = sds_build_broadcast("example.org", 2, svc, 1, NULL, NULL, NULL, sd->buf[1], DOC_CAP);
  sd->sd.docs[0] = sd->buf[0];
  sd->sd.docs[1] = sd->buf[1];
  ck_assert_uint_gt(sd->sd.lens[0], 0u);
  ck_assert_uint_gt(sd->sd.lens[1], 0u);
}

static unsigned count_of(const char *hay, const char *needle) {
  unsigned n = 0;

  for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += strlen(needle)) n++;
  return n;
}

static char *slurp_text(const char *path) {
  FILE *f = fopen(path, "r");
  char *buf = calloc(1, 65536);
  size_t n;

  ck_assert_ptr_nonnull(f);
  ck_assert_ptr_nonnull(buf);
  n = fread(buf, 1, 65535, f);
  buf[n] = '\0';
  fclose(f);
  return buf;
}

typedef struct {
  const send_step_t *steps;
  unsigned nsteps;
  out_fmt_t format;
  int verbose;
  unsigned segments;
  unsigned services;
  const char *log_has;
  const char *out_has;
} listen_case_t;

static const send_step_t steps_one[] = {{DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0}};
static const send_step_t steps_other_and_dup[] = {
  {DVBSTP_PAYLOAD_SP_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_PACKAGE_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
};
static const send_step_t steps_two_versions[] = {
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 2, 1, 0},
};

static const listen_case_t listen_cases[] = {
  {steps_one, 1, OUT_CSV, 0, 1, 2, "found 2 services in 1 segment", "Svc B,rtp://@239.1.1.2:5000,1,2,102"},
  {steps_one, 1, OUT_CSV, 1, 1, 2, "segment 1: 2 services", "Svc A,rtp://@239.1.1.1:5000,1,2,101"},
  {steps_other_and_dup, 4, OUT_CSV, 0, 1, 2, "found 2 services in 1 segment", "Svc A"},
  {steps_two_versions, 2, OUT_CSV, 0, 1, 1, "found 1 service in 1 segment", "Svc A"},
  {steps_one, 1, OUT_XML, 0, 1, 2, "found 2 services in 1 segment", "<BroadcastDiscovery"},
};

START_TEST(listen_run_collects_broadcast_discovery_in_every_format) {
  const listen_case_t *c = &listen_cases[_i];
  sds_sender_t *sd = calloc(1, sizeof *sd);
  char group[32];
  char out_path[64];
  char msg[8192];
  char want[64];
  char *out;
  pthread_t th;
  config_t cfg;
  int fd;
  int rc;

  ck_assert_ptr_nonnull(sd);
  build_docs(sd);
  run_helper_group(group, sizeof group, 79);
  sd->sd.group = group;
  sd->sd.port = run_helper_free_udp_port();
  sd->sd.steps = c->steps;
  sd->sd.nsteps = c->nsteps;
  snprintf(out_path, sizeof out_path, "/tmp/dipisds_listen_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = sd->sd.port;
  cfg.timeout_s = 20;
  cfg.output_path = out_path;
  cfg.format = c->format;
  cfg.verbose = c->verbose;
  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, dvbstp_sender_thread, &sd->sd), 0);
  rc = listen_run(&cfg);
  pthread_join(th, NULL);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, c->log_has));
  out = slurp_text(out_path);
  ck_assert_ptr_nonnull(strstr(out, c->out_has));
  if (c->format == OUT_CSV) {
    ck_assert_uint_eq(count_of(out, "rtp://"), c->services);
  } else {
    ck_assert_uint_eq(count_of(out, "<BroadcastDiscovery"), c->segments);
  }
  snprintf(want, sizeof want, "in %u segment", c->segments);
  ck_assert_ptr_nonnull(strstr(msg, want));
  free(out);
  free(sd);
  unlink(out_path);
}
END_TEST

typedef enum { END_TIMEOUT, END_JOIN_FAILS, END_OUTPUT_OPEN_FAILS, END_OUTPUT_WRITE_FAILS } end_kind_t;

typedef struct {
  end_kind_t kind;
  int rc;
  const char *msg;
} end_case_t;

static const end_case_t end_cases[] = {
  {END_TIMEOUT, 0, "found 0 services in 0 segments"},
  {END_JOIN_FAILS, 1, "cannot join"},
  {END_OUTPUT_OPEN_FAILS, 1, "for writing"},
  {END_OUTPUT_WRITE_FAILS, 1, "error writing"},
};

START_TEST(listen_run_handles_timeout_and_setup_failures) {
  const end_case_t *c = &end_cases[_i];
  char group[32];
  char out_path[64];
  char msg[4096];
  config_t cfg;
  int fd;
  int rc;

  snprintf(out_path, sizeof out_path, "/tmp/dipisds_listen_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  run_helper_group(group, sizeof group, 80);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = run_helper_free_udp_port();
  cfg.timeout_s = 1;
  cfg.output_path = out_path;
  cfg.format = c->kind == END_OUTPUT_WRITE_FAILS ? OUT_M3U : OUT_CSV;
  if (c->kind == END_JOIN_FAILS) cfg.iface = "nosuchif0";
  if (c->kind == END_OUTPUT_OPEN_FAILS) cfg.output_path = "/nonexistent-dir/out.csv";
  if (c->kind == END_OUTPUT_WRITE_FAILS) cfg.output_path = "/dev/full";
  log_capture_begin();
  rc = listen_run(&cfg);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, c->rc);
  ck_assert_ptr_nonnull(strstr(msg, c->msg));
  unlink(out_path);
}
END_TEST

static Suite *listen_suite(void) {
  Suite *s = suite_create("dipisds_listen");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, already_seen_is_false_on_first_sighting);
  tcase_add_test(tc, already_seen_is_true_on_repeat);
  tcase_add_test(tc, already_seen_distinguishes_by_all_three_fields);
  tcase_add_test(tc, already_seen_stops_recording_past_the_cap);
  tcase_add_loop_test(tc, listen_run_collects_broadcast_discovery_in_every_format, 0, (int)(sizeof listen_cases / sizeof listen_cases[0]));
  tcase_add_loop_test(tc, listen_run_handles_timeout_and_setup_failures, 0, (int)(sizeof end_cases / sizeof end_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(listen_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
