/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../log_capture.h"
#include "../run_helper.h"
#include "dipibcg/container.h"
#include "dipibcg/listen.h"
#include "dipibcg/wrapper.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/net/dvbstp.h"
#include "lib/sys/ioutil.h"
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

START_TEST(write_csvmap_writes_only_channels_with_uri) {
  char path[] = "/tmp/dvbipitools_test_listen_csvmap_XXXXXX";
  bcg_doc_t doc;
  bcg_channel_t *c;
  FILE *f;
  char line[256];
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "channel1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "channel2"); /* no uri: excluded */

  write_csvmap(path, &doc);

  f = fopen(path, "r");
  ck_assert_ptr_nonnull(f);
  ck_assert_ptr_nonnull(fgets(line, sizeof line, f));
  ck_assert_str_eq(line, "channel1,rtp://239.1.1.1:5000,1,2,101\n");
  ck_assert_ptr_null(fgets(line, sizeof line, f));
  fclose(f);

  bcg_doc_free(&doc);
  unlink(path);
}
END_TEST

#define GARBAGE_LEN 40

typedef struct {
  dvbstp_sender_t sd;
  unsigned char *valid;
  unsigned char garbage[GARBAGE_LEN];
} bcg_sender_t;

static void build_wrapped(bcg_sender_t *b, int compress) {
  bcg_doc_t doc;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  accessunit_scratch_t sc;
  bitwriter_t bw;
  strrepo_writer_t sw;
  unsigned char *cont;
  size_t cont_len;
  size_t bits_len;
  size_t strs_len;
  const unsigned char *bits;
  const unsigned char *strs;
  int nfuu = 0;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "ch1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  bufcpy(c->names[0], sizeof c->names[0], "Channel One");
  c->name_count = 1;
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "ch1");
  bufcpy(pr->start, sizeof pr->start, "2030-01-01T12:00:00Z");
  bufcpy(pr->stop, sizeof pr->stop, "2030-01-01T13:00:00Z");
  bufcpy(pr->title, sizeof pr->title, "News");
  accessunit_scratch_init(&sc);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(accessunit_encode(&sc, &doc, &bw, &sw, &nfuu), 0);
  bits = bitwriter_data(&bw, &bits_len);
  strs = strrepo_writer_data(&sw, &strs_len);
  ck_assert_int_eq(container_build(bits, bits_len, strs, strs_len, &cont, &cont_len), 0);
  ck_assert_int_eq(wrapper_build(cont, cont_len, compress, &b->valid, &b->sd.lens[0]), 0);
  b->sd.docs[0] = b->valid;
  free(cont);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  accessunit_scratch_free(&sc);
  bcg_doc_free(&doc);
  memset(b->garbage, 0, sizeof b->garbage);
  b->garbage[0] = WRAPPER_METHOD_NONE;
  memset(b->garbage + 1, 0x5A, sizeof b->garbage - 1);
  b->sd.docs[1] = b->garbage;
  b->sd.lens[1] = sizeof b->garbage;
}

static const send_step_t bcg_valid[] = {{DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 1}};
static const send_step_t bcg_garbage_then_valid_with_noise[] = {
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 1, 1},
  {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 1, 1},
  {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 2, 0, 1},
};
static const send_step_t bcg_garbage_only[] = {{DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 1, 1}};

typedef struct {
  const send_step_t *steps;
  unsigned nsteps;
  int compress;
  int with_csvmap;
  int bad_output;
  int rc;
  const char *log_has;
  const char *out_has;
} bcg_listen_case_t;

static const bcg_listen_case_t bcg_listen_cases[] = {
  {bcg_valid, 1, 0, 1, 0, 0, "captured 1 time in 1 segment", "<channel id=\"ch1\""},
  {bcg_garbage_then_valid_with_noise, 4, 0, 0, 0, 0, "captured 1 time in 2 segments", "<title"},
  {bcg_garbage_only, 1, 0, 0, 0, 1, "captured 0 times in 1 segment", NULL},
  {bcg_valid, 1, 0, 0, 1, 1, "cannot open", NULL},
#ifdef DIPIBCG_TEST_ZLIB
  {bcg_valid, 1, 1, 1, 0, 0, "captured 1 time in 1 segment", "<channel id=\"ch1\""},
#endif
};

START_TEST(listen_run_captures_a_wrapped_container_and_skips_bad_segments) {
  const bcg_listen_case_t *c = &bcg_listen_cases[_i];
  bcg_sender_t *b = calloc(1, sizeof *b);
  char group[32];
  char out_path[64];
  char map_path[64];
  char msg[8192];
  char text[16384];
  pthread_t th;
  config_t cfg;
  FILE *f;
  size_t n;
  int fd;
  int rc;

  ck_assert_ptr_nonnull(b);
  build_wrapped(b, c->compress);
  if (c->compress) ck_assert_uint_eq(b->valid[0], WRAPPER_METHOD_ZLIB);
  run_helper_group(group, sizeof group, 83);
  b->sd.group = group;
  b->sd.port = run_helper_free_udp_port();
  b->sd.steps = c->steps;
  b->sd.nsteps = c->nsteps;
  snprintf(out_path, sizeof out_path, "/tmp/dipibcg_listen_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  snprintf(map_path, sizeof map_path, "/tmp/dipibcg_listenmap_XXXXXX");
  fd = mkstemp(map_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = b->sd.port;
  cfg.timeout_s = 20;
  cfg.output_path = c->bad_output ? "/nonexistent-dir/out.xml" : out_path;
  cfg.csvmap_path = c->with_csvmap ? map_path : NULL;
  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, dvbstp_sender_thread, &b->sd), 0);
  rc = listen_run(&cfg);
  pthread_join(th, NULL);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, c->rc);
  ck_assert_ptr_nonnull(strstr(msg, c->log_has));
  if (c->out_has) {
    f = fopen(out_path, "r");
    ck_assert_ptr_nonnull(f);
    n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    ck_assert_ptr_nonnull(strstr(text, c->out_has));
  }
  if (c->with_csvmap) {
    f = fopen(map_path, "r");
    ck_assert_ptr_nonnull(f);
    n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    ck_assert_ptr_nonnull(strstr(text, "ch1,rtp://239.1.1.1:5000,1,2,101"));
  }
  free(b->valid);
  free(b);
  unlink(out_path);
  unlink(map_path);
}
END_TEST

static Suite *listen_suite(void) {
  Suite *s = suite_create("dipibcg_listen");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, already_seen_is_false_on_first_sighting);
  tcase_add_test(tc, already_seen_is_true_on_repeat);
  tcase_add_test(tc, already_seen_distinguishes_by_all_three_fields);
  tcase_add_test(tc, already_seen_stops_recording_past_the_cap);
  tcase_add_test(tc, write_csvmap_writes_only_channels_with_uri);
  tcase_add_loop_test(tc, listen_run_captures_a_wrapped_container_and_skips_bad_segments, 0, (int)(sizeof bcg_listen_cases / sizeof bcg_listen_cases[0]));
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
