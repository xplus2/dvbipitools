/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/fragment.h"
#include "lib/sys/ioutil.h"
#include "lib/tva/tva_xml.h"

typedef struct {
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
} io_t;

static void io_open(io_t *io) {
  bitwriter_init(&io->bw);
  ck_assert_int_eq(strrepo_writer_init(&io->sw), 0);
}

static void io_reader(io_t *io) {
  size_t bl;
  size_t sl;
  const unsigned char *bd = bitwriter_data(&io->bw, &bl);
  const unsigned char *sd = strrepo_writer_data(&io->sw, &sl);
  bitreader_init(&io->br, bd, bl);
  ck_assert_int_eq(strrepo_reader_init(&io->sr, sd, sl), 0);
}

static void io_close(io_t *io) {
  bitwriter_free(&io->bw);
  strrepo_writer_free(&io->sw);
}

static int enc_tree(io_t *io, const bim_type_t *t, bim_node_t *root) {
  int rc = bim_encode_payload(t, root, &io->bw, &io->sw);
  bim_node_free(root);
  return rc;
}

static bim_node_t *pi_tree(const char *id) {
  bim_node_t *root = bim_node_new("ProgramInformation");
  ck_assert_ptr_nonnull(root);
  ck_assert_int_eq(bim_node_set_attr(root, "programId", id), 0);
  ck_assert_ptr_nonnull(bim_node_add_new(root, "BasicDescription", NULL));
  return root;
}

static bim_node_t *bd_of(const bim_node_t *root) {
  return root->kids[0];
}

static int pi_roundtrip(bim_node_t *root, char *crid, size_t cap, bcg_programme_t *out) {
  io_t io;
  int rc;
  io_open(&io);
  ck_assert_int_eq(enc_tree(&io, tva_type_program_information, root), 0);
  io_reader(&io);
  rc = fragment_decode_program_information(&io.br, &io.sr, crid, cap, out);
  io_close(&io);
  return rc;
}

START_TEST(pi_decode_first_of_each_text_wins) {
  bim_node_t *root = pi_tree("crid://x/1");
  bim_node_t *g1;
  bim_node_t *g2;
  bcg_programme_t out;
  char crid[128];
  ck_assert_ptr_nonnull(bim_node_add_new(bd_of(root), "Title", "first"));
  ck_assert_ptr_nonnull(bim_node_add_new(bd_of(root), "Title", "second"));
  {
    bim_node_t *st = bim_node_add_new(bd_of(root), "ShortTitle", "short");
    ck_assert_ptr_nonnull(st);
    ck_assert_int_eq(bim_node_set_attr(st, "length", "5"), 0);
  }
  ck_assert_ptr_nonnull(bim_node_add_new(bd_of(root), "Synopsis", "syn1"));
  ck_assert_ptr_nonnull(bim_node_add_new(bd_of(root), "Synopsis", "syn2"));
  g1 = bim_node_add_new(bd_of(root), "Genre", NULL);
  ck_assert_int_eq(bim_node_set_attr(g1, "href", tva_genre_href("cat1")), 0);
  ck_assert_ptr_nonnull(bim_node_add_new(g1, "Name", "cat1"));
  g2 = bim_node_add_new(bd_of(root), "Genre", NULL);
  ck_assert_int_eq(bim_node_set_attr(g2, "href", tva_genre_href("cat2")), 0);
  ck_assert_ptr_nonnull(bim_node_add_new(g2, "Name", "cat2"));
  ck_assert_int_eq(pi_roundtrip(root, crid, sizeof crid, &out), 0);
  ck_assert_str_eq(out.title, "first");
  ck_assert_str_eq(out.desc, "syn1");
  ck_assert_str_eq(out.category, "cat1");
  bcg_programme_free_text(&out);
}
END_TEST

START_TEST(pi_decode_genre_without_name_and_without_text) {
  bim_node_t *root = pi_tree("crid://x/2");
  bim_node_t *g;
  bcg_programme_t out;
  char crid[128];
  ck_assert_ptr_nonnull(bim_node_add_new(bd_of(root), "Title", NULL));
  g = bim_node_add_new(bd_of(root), "Genre", NULL);
  ck_assert_int_eq(bim_node_set_attr(g, "href", tva_genre_href("cat1")), 0);
  ck_assert_int_eq(pi_roundtrip(root, crid, sizeof crid, &out), 0);
  ck_assert_ptr_null(out.category);
  ck_assert_ptr_null(out.title);
  bcg_programme_free_text(&out);
}
END_TEST

START_TEST(pi_decode_rejects_small_crid_buffer) {
  bcg_programme_t out;
  char crid[8];
  ck_assert_int_eq(pi_roundtrip(pi_tree("crid://example/long-identifier"), crid, sizeof crid, &out), -1);
}
END_TEST

START_TEST(pi_decode_rejects_truncated_payload) {
  io_t io;
  bcg_programme_t out;
  char crid[128];
  size_t bl;
  io_open(&io);
  ck_assert_int_eq(enc_tree(&io, tva_type_program_information, pi_tree("crid://x/3")), 0);
  (void)bitwriter_data(&io.bw, &bl);
  io_reader(&io);
  io.br.len = bl / 2;
  ck_assert_int_eq(fragment_decode_program_information(&io.br, &io.sr, crid, sizeof crid, &out), -1);
  io_close(&io);
}
END_TEST

static bim_node_t *sched_tree(const char *service, unsigned nev, int with_stop, int with_start) {
  bim_node_t *root = bim_node_new("Schedule");
  ck_assert_ptr_nonnull(root);
  ck_assert_int_eq(bim_node_set_attr(root, "serviceIDRef", service), 0);
  for (unsigned i = 0; i < nev; i++) {
    char crid[32];
    bim_node_t *ev = bim_node_add_new(root, "ScheduleEvent", NULL);
    bim_node_t *p = bim_node_add_new(ev, "Program", NULL);
    snprintf(crid, sizeof crid, "crid://x/%u", i);
    ck_assert_int_eq(bim_node_set_attr(p, "crid", crid), 0);
    if (with_start) ck_assert_ptr_nonnull(bim_node_add_new(ev, "PublishedStartTime", "2020-12-15T12:00:00Z"));
    if (with_stop) ck_assert_ptr_nonnull(bim_node_add_new(ev, "PublishedEndTime", "2020-12-15T13:00:00Z"));
  }
  return root;
}

static int g_lookups;
static int g_lookup_rc;

static int lookup_cb(void *ctx, const char *crid, bcg_programme_t *pr) {
  (void)ctx;
  (void)crid;
  (void)pr;
  g_lookups++;
  return g_lookup_rc;
}

static int sched_roundtrip(bim_node_t *root, bcg_doc_t *doc, fragment_text_lookup_fn fn) {
  io_t io;
  int rc;
  io_open(&io);
  ck_assert_int_eq(enc_tree(&io, tva_type_schedule, root), 0);
  io_reader(&io);
  rc = fragment_decode_schedule(&io.br, &io.sr, doc, fn, NULL);
  io_close(&io);
  return rc;
}

START_TEST(schedule_decode_lookup_outcomes) {
  static const int rcs[] = {0, -1, -2};
  bcg_doc_t doc;
  bcg_doc_init(&doc);
  g_lookups = 0;
  g_lookup_rc = rcs[_i];
  ck_assert_int_eq(sched_roundtrip(sched_tree("chan", 2, 1, 1), &doc, lookup_cb), _i == 2 ? -1 : 0);
  ck_assert_int_eq(g_lookups, _i == 2 ? 1 : 2);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(schedule_decode_without_lookup_and_stop) {
  bcg_doc_t doc;
  bcg_doc_init(&doc);
  ck_assert_int_eq(sched_roundtrip(sched_tree("chan", 2, 0, 1), &doc, NULL), 0);
  ck_assert_int_eq(doc.programme_count, 2);
  ck_assert_str_eq(doc.programmes[0].stop, "");
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(schedule_decode_skips_events_without_start) {
  bcg_doc_t doc;
  bcg_doc_init(&doc);
  ck_assert_int_eq(sched_roundtrip(sched_tree("chan", 2, 1, 0), &doc, lookup_cb), 0);
  ck_assert_int_eq(doc.programme_count, 0);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(schedule_decode_rejects_oversized_channel_id) {
  bcg_doc_t doc;
  char big[BCG_ID_LEN + 8];
  memset(big, 'c', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  bcg_doc_init(&doc);
  ck_assert_int_eq(sched_roundtrip(sched_tree(big, 1, 1, 1), &doc, NULL), -1);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(schedule_decode_rejects_truncated_payload) {
  io_t io;
  bcg_doc_t doc;
  size_t bl;
  bcg_doc_init(&doc);
  io_open(&io);
  ck_assert_int_eq(enc_tree(&io, tva_type_schedule, sched_tree("chan", 2, 1, 1)), 0);
  (void)bitwriter_data(&io.bw, &bl);
  io_reader(&io);
  io.br.len = bl / 2;
  ck_assert_int_eq(fragment_decode_schedule(&io.br, &io.sr, &doc, NULL, NULL), -1);
  io_close(&io);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(schedule_encode_chain_and_empty_chain) {
  bcg_programme_t prs[3];
  int next[3] = {2, -1, 1};
  io_t io;
  memset(prs, 0, sizeof prs);
  for (int i = 0; i < 3; i++) {
    bufcpy(prs[i].channel_id, sizeof prs[i].channel_id, "chan");
    bufcpy(prs[i].start, sizeof prs[i].start, "2020-12-15T12:00:00Z");
    if (i != 1) bufcpy(prs[i].stop, sizeof prs[i].stop, "2020-12-15T13:00:00Z");
  }
  io_open(&io);
  ck_assert_int_eq(fragment_encode_schedule("chan", prs, 0, next, &io.bw, &io.sw), 0);
  io_close(&io);
  io_open(&io);
  ck_assert_int_eq(fragment_encode_schedule("chan", prs, -1, next, &io.bw, &io.sw), -1);
  io_close(&io);
  io_open(&io);
  bufcpy(prs[0].start, sizeof prs[0].start, "garbage");
  ck_assert_int_eq(fragment_encode_schedule("chan", prs, 0, next, &io.bw, &io.sw), -1);
  io_close(&io);
}
END_TEST

static bim_node_t *si_tree(const char *id, unsigned nnames, const char *name, const char *url) {
  bim_node_t *root = bim_node_new("ServiceInformation");
  ck_assert_ptr_nonnull(root);
  ck_assert_int_eq(bim_node_set_attr(root, "serviceId", id), 0);
  for (unsigned i = 0; i < nnames; i++) ck_assert_ptr_nonnull(bim_node_add_new(root, "Name", name));
  if (url) ck_assert_ptr_nonnull(bim_node_add_new(root, "ServiceURL", url));
  return root;
}

static int si_decode(bim_node_t *root, const char *tail0, const char *tail1, bcg_channel_t *out, size_t *left) {
  io_t io;
  int rc;
  io_open(&io);
  ck_assert_int_eq(enc_tree(&io, tva_type_service_information, root), 0);
  if (tail0) ck_assert_int_eq(dvb_string_encode(&io.sw, tail0), 0);
  if (tail1) ck_assert_int_eq(dvb_string_encode(&io.sw, tail1), 0);
  io_reader(&io);
  rc = fragment_decode_service_information(&io.br, &io.sr, out);
  if (left) *left = io.sr.len - io.sr.pos;
  io_close(&io);
  return rc;
}

START_TEST(service_decode_tail_forms) {
  static const struct {
    const char *t0;
    const char *t1;
    int tail_consumed;
    int names;
    unsigned sid;
  } t[] = {
      {"dvb://1.2.3", "0", 1, 1, 3},
      {"", "0", 1, 1, 0},
      {"dvb://1.2.3", "1", 1, 0, 3},
      {"dvb://zz", "0", 1, 1, 0},
      {"http://x", "0", 0, 1, 0},
      {"dvb://1.2.3", "2", 0, 1, 0},
      {"dvb://1.2.3", NULL, 0, 1, 0},
      {NULL, NULL, -1, 1, 0},
  };
  bcg_channel_t c;
  size_t left;
  ck_assert_int_eq(si_decode(si_tree("svc", 1, "Name", "http://svc"), t[_i].t0, t[_i].t1, &c, &left), 0);
  ck_assert_int_eq(c.name_count, t[_i].names);
  ck_assert_uint_eq(c.sid, t[_i].sid);
  ck_assert_str_eq(c.uri, "http://svc");
  if (t[_i].tail_consumed > 0) ck_assert_uint_eq(left, 0u);
  else if (t[_i].tail_consumed == 0) ck_assert_uint_gt(left, 0u);
}
END_TEST

START_TEST(service_decode_drops_excess_and_oversized_names) {
  bcg_channel_t c;
  char big[BCG_ID_LEN + 8];
  memset(big, 'n', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  ck_assert_int_eq(si_decode(si_tree("svc", 12, "n", NULL), NULL, NULL, &c, NULL), 0);
  ck_assert_int_eq(c.name_count, BCG_MAX_NAMES);
  ck_assert_int_eq(si_decode(si_tree("svc", 2, big, NULL), NULL, NULL, &c, NULL), 0);
  ck_assert_int_eq(c.name_count, 0);
}
END_TEST

START_TEST(service_decode_oversized_fields) {
  bcg_channel_t c;
  char big[BCG_ID_LEN + 8];
  memset(big, 'u', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  ck_assert_int_eq(si_decode(si_tree("svc", 1, "n", big), NULL, NULL, &c, NULL), 0);
  ck_assert_str_eq(c.uri, "");
  ck_assert_int_eq(si_decode(si_tree(big, 1, "n", NULL), NULL, NULL, &c, NULL), -1);
}
END_TEST

START_TEST(service_decode_rejects_truncated_payload) {
  io_t io;
  bcg_channel_t c;
  size_t bl;
  io_open(&io);
  ck_assert_int_eq(enc_tree(&io, tva_type_service_information, si_tree("svc", 1, "n", NULL)), 0);
  (void)bitwriter_data(&io.bw, &bl);
  io_reader(&io);
  io.br.len = bl / 2;
  ck_assert_int_eq(fragment_decode_service_information(&io.br, &io.sr, &c), -1);
  io_close(&io);
}
END_TEST

START_TEST(service_encode_name_and_triplet_forms) {
  static const struct {
    int names;
    unsigned onid;
    unsigned tsid;
    unsigned sid;
    const char *dtt;
    const char *synth;
  } t[] = {
      {0, 0, 0, 0, "", "1"},
      {1, 0, 0, 0, "", "0"},
      {2, 1, 2, 3, "dvb://1.2.3", "0"},
      {0, 0, 0, 5, "dvb://0.0.5", "1"},
  };
  bcg_channel_t c;
  io_t io;
  char s0[64];
  char s1[8];
  memset(&c, 0, sizeof c);
  bufcpy(c.id, sizeof c.id, "svc");
  bufcpy(c.uri, sizeof c.uri, "http://svc");
  bufcpy(c.names[0], sizeof c.names[0], "A");
  bufcpy(c.names[1], sizeof c.names[1], "B");
  c.name_count = t[_i].names;
  c.onid = t[_i].onid;
  c.tsid = t[_i].tsid;
  c.sid = t[_i].sid;
  io_open(&io);
  ck_assert_int_eq(fragment_encode_service_information(&c, &io.bw, &io.sw), 0);
  io_reader(&io);
  {
    bim_node_t *root;
    ck_assert_int_eq(bim_decode_payload(tva_type_service_information, "ServiceInformation", &io.br, &io.sr, &root), 0);
    bim_node_free(root);
  }
  ck_assert_int_eq(dvb_string_decode(&io.sr, s0, sizeof s0), 0);
  ck_assert_int_eq(dvb_string_decode(&io.sr, s1, sizeof s1), 0);
  ck_assert_str_eq(s0, t[_i].dtt);
  ck_assert_str_eq(s1, t[_i].synth);
  io_close(&io);
}
END_TEST

START_TEST(service_encode_rejects_empty_uri) {
  bcg_channel_t c;
  io_t io;
  memset(&c, 0, sizeof c);
  bufcpy(c.id, sizeof c.id, "svc");
  io_open(&io);
  (void)fragment_encode_service_information(&c, &io.bw, &io.sw);
  io_close(&io);
}
END_TEST

START_TEST(pi_encode_optional_fields) {
  bcg_programme_t pr;
  io_t io;
  memset(&pr, 0, sizeof pr);
  bufcpy(pr.channel_id, sizeof pr.channel_id, "chan");
  bufcpy(pr.start, sizeof pr.start, "2020-12-15T12:00:00Z");
  ck_assert_int_eq(bcg_set_text(&pr.desc, "only synopsis"), 0);
  io_open(&io);
  ck_assert_int_eq(fragment_encode_program_information(&pr, &io.bw, &io.sw), 0);
  io_close(&io);
  bcg_programme_free_text(&pr);
  memset(&pr, 0, sizeof pr);
  bufcpy(pr.channel_id, sizeof pr.channel_id, "chan");
  bufcpy(pr.start, sizeof pr.start, "2020-12-15T12:00:00Z");
  ck_assert_int_eq(bcg_set_text(&pr.category, "only genre"), 0);
  io_open(&io);
  ck_assert_int_eq(fragment_encode_program_information(&pr, &io.bw, &io.sw), 0);
  io_close(&io);
  bcg_programme_free_text(&pr);
}
END_TEST

static Suite *coding_suite(void) {
  Suite *s = suite_create("coding");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pi_decode_first_of_each_text_wins);
  tcase_add_test(tc, pi_decode_genre_without_name_and_without_text);
  tcase_add_test(tc, pi_decode_rejects_small_crid_buffer);
  tcase_add_test(tc, pi_decode_rejects_truncated_payload);
  tcase_add_loop_test(tc, schedule_decode_lookup_outcomes, 0, 3);
  tcase_add_test(tc, schedule_decode_without_lookup_and_stop);
  tcase_add_test(tc, schedule_decode_skips_events_without_start);
  tcase_add_test(tc, schedule_decode_rejects_oversized_channel_id);
  tcase_add_test(tc, schedule_decode_rejects_truncated_payload);
  tcase_add_test(tc, schedule_encode_chain_and_empty_chain);
  tcase_add_loop_test(tc, service_decode_tail_forms, 0, 8);
  tcase_add_test(tc, service_decode_drops_excess_and_oversized_names);
  tcase_add_test(tc, service_decode_oversized_fields);
  tcase_add_test(tc, service_decode_rejects_truncated_payload);
  tcase_add_loop_test(tc, service_encode_name_and_triplet_forms, 0, 4);
  tcase_add_test(tc, service_encode_rejects_empty_uri);
  tcase_add_test(tc, pi_encode_optional_fields);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(coding_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
