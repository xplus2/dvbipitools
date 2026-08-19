/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi/psi.h"

#include "lib/mux/mpts.h"

#define MAX_SEEN 256

static unsigned g_pids[MAX_SEEN];
static unsigned char g_ccs[MAX_SEEN];
static unsigned char g_pkts[MAX_SEEN][188];
static int g_count;

static void capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if (g_count < MAX_SEEN) {
    g_pids[g_count] = (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
    g_ccs[g_count] = pkt[3] & 0x0F;
    memcpy(g_pkts[g_count], pkt, 188);
  }
  g_count++;
}

static int saw_pid(unsigned pid) {
  for (int i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == pid)
      return 1;
  return 0;
}

static int count_pid(unsigned pid) {
  int n = 0;
  for (int i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == pid)
      n++;
  return n;
}

/* first captured packet for pid, NULL if none. pointer_field(1) + section starts at pkt[4] -
   same layout wrap_ts_packet() below produces */
static const unsigned char *first_pkt(unsigned pid) {
  for (int i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == pid)
      return g_pkts[i];
  return NULL;
}

/* short sections get padded via an adaptation field ahead of the payload (see write_packet() in
   tspacket_write.c), so the pointer_field isn't always at pkt[4] - skip it the same way psi_feed()
   does. version_number is section byte 5 (table_id[1]+flags/length[2]+tsid[2]+version). */
static unsigned sdt_version(const unsigned char *pkt) {
  unsigned afc = (pkt[3] >> 4) & 0x3;
  size_t off = (afc == 3) ? 5 + (size_t)pkt[4] : 4;
  const unsigned char *section = pkt + off + 1; /* +1 skips pointer_field */
  return (section[5] >> 1) & 0x1F;
}

/* wraps one PSI section into a single 188-byte TS packet, pusi=1, pointer=0 */
static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

/* mock per-program ops: no dependency on any tool's real packetizer type */
typedef struct {
  unsigned char tag;
  int pending;
} mock_program_t;

static int mock_get_sdt_info(void *ctx, psi_sdt_entry_t *out) {
  mock_program_t *p = ctx;
  out->service_id = p->tag;
  out->service_type = 0x01;
  out->provider = "prov";
  out->service_name = "svc";
  return 0;
}

static size_t mock_build_eit(void *ctx, unsigned char *out, size_t cap) {
  mock_program_t *p = ctx;
  if (cap < 4)
    return 0;
  memset(out, (unsigned char)(p->tag + 1), 4);
  return 4;
}

static int mock_eit_pending(const void *ctx) {
  const mock_program_t *p = ctx;
  return p->pending;
}

static const mpts_program_ops_t mock_program_ops = {mock_get_sdt_info, mock_build_eit, mock_eit_pending};

/* mock CAS ops */
typedef struct {
  int ecm_due;   /* 1 = cas_ops.ecm_due() fills a section this call, then clears itself */
  int emm_queue; /* number of next_emm() calls that succeed before returning empty */
} mock_cas_t;

static size_t mock_build_cat(void *ctx, unsigned char *out, size_t cap) {
  (void)ctx;
  if (cap < 4)
    return 0;
  memset(out, 0xCA, 4);
  return 4;
}

static int mock_ecm_due(void *ctx, size_t vendor_idx, double now_s, unsigned char *out, size_t cap, size_t *out_len) {
  mock_cas_t *c = ctx;
  (void)vendor_idx;
  (void)now_s;
  if (!c->ecm_due)
    return -1;
  c->ecm_due = 0;
  if (cap < 4)
    return -1;
  memset(out, 0xEC, 4);
  *out_len = 4;
  return 0;
}

static int mock_next_emm(void *ctx, size_t vendor_idx, unsigned char *out, size_t cap, size_t *out_len) {
  mock_cas_t *c = ctx;
  (void)vendor_idx;
  if (c->emm_queue <= 0)
    return -1;
  c->emm_queue--;
  if (cap < 4)
    return -1;
  memset(out, 0xEA, 4);
  *out_len = 4;
  return 0;
}

static const mpts_cas_ops_t mock_cas_ops = {mock_build_cat, mock_ecm_due, mock_next_emm};

START_TEST(mpts_tick_emits_multi_program_pat) {
  psi_pat_entry_t entries[2] = {{101, 0x1000}, {102, 0x1001}};
  mpts_t *m = mpts_new(0x1234, 1, "", entries, 2, &mock_program_ops);
  unsigned char pkt[188], sec[64];
  psi_t *p;
  int count;
  const psi_program_t *progs;
  size_t slen;

  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);
  ck_assert(saw_pid(0x0000));

  slen = psi_build_pat_multi(0x1234, 0, entries, 2, sec, sizeof sec);
  ck_assert_uint_ne(slen, 0u);
  p = psi_new();
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  psi_feed(p, pkt);
  ck_assert_int_eq(psi_have_pat(p), 1);
  progs = psi_pat_programs(p, &count);
  ck_assert_int_eq(count, 2);
  ck_assert_uint_eq(progs[0].program_number, 101u);
  ck_assert_uint_eq(progs[0].pmt_pid, 0x1000u);
  ck_assert_uint_eq(progs[1].program_number, 102u);
  ck_assert_uint_eq(progs[1].pmt_pid, 0x1001u);

  psi_free(p);
  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_omits_nit_when_no_network_name) {
  psi_pat_entry_t entries[1] = {{101, 0x1000}};
  mpts_t *m = mpts_new(1, 1, "", entries, 1, &mock_program_ops);

  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);
  ck_assert(!saw_pid(0x0010));

  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_emits_nit_when_network_name_set) {
  psi_pat_entry_t entries[1] = {{101, 0x1000}};
  mpts_t *m = mpts_new(1, 1, "My Network", entries, 1, &mock_program_ops);

  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);
  ck_assert(saw_pid(0x0010));

  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_skips_sdt_eit_for_inactive_program_but_keeps_pat_entry) {
  psi_pat_entry_t entries[2] = {{101, 0x1000}, {102, 0x1001}};
  mpts_t *m = mpts_new(1, 1, "", entries, 2, &mock_program_ops);
  mock_program_t prog0 = {0x10, 0};

  mpts_set_program(m, 0, &prog0);
  /* program 1 stays inactive (NULL) - down or not connected yet */

  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);

  ck_assert(saw_pid(0x0000)); /* PAT still lists both programs */
  ck_assert(saw_pid(0x0011)); /* SDT for the active program */
  ck_assert(saw_pid(0x0012)); /* EIT for the active program */

  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_shortly_after_sends_nothing_new) {
  psi_pat_entry_t entries[1] = {{101, 0x1000}};
  mpts_t *m = mpts_new(1, 1, "My Network", entries, 1, &mock_program_ops);
  mock_program_t prog0 = {0x10, 0};

  mpts_set_program(m, 0, &prog0);
  mpts_tick(m, 0.0, capture_cb, NULL); /* primes all timers */

  g_count = 0;
  mpts_tick(m, 0.05, capture_cb, NULL); /* well under any interval */

  ck_assert_int_eq(g_count, 0);

  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_resends_eit_immediately_on_metadata_change) {
  psi_pat_entry_t entries[1] = {{101, 0x1000}};
  mpts_t *m = mpts_new(1, 1, "", entries, 1, &mock_program_ops);
  mock_program_t prog0 = {0x10, 0};

  mpts_set_program(m, 0, &prog0);
  mpts_tick(m, 0.0, capture_cb, NULL);
  prog0.pending = 1; /* simulates a metadata change since the last build_eit() */

  g_count = 0;
  mpts_tick(m, 0.05, capture_cb, NULL); /* EIT timer not due, but metadata changed */

  ck_assert(saw_pid(0x0012));
  ck_assert(!saw_pid(0x0000)); /* PAT/SDT still not due */
  ck_assert(!saw_pid(0x0011));

  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_emits_one_composite_sdt_section_for_all_active_programs) {
  psi_pat_entry_t entries[2] = {{101, 0x1000}, {102, 0x1001}};
  mpts_t *m = mpts_new(1, 1, "", entries, 2, &mock_program_ops);
  mock_program_t prog0 = {101, 0}, prog1 = {102, 0};
  unsigned char patpkt[188], sec[64];
  size_t slen;
  const unsigned char *pkt;
  psi_t *p;
  int count;
  const psi_multi_program_t *progs;

  mpts_set_program(m, 0, &prog0);
  mpts_set_program(m, 1, &prog1);

  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);

  /* the actual bug: two colliding sections (same table_id/tsid/onid/version, each claiming
     section_number/last_section_number 0/0) instead of one real sub_table - see EN 300 468
     5.2.3/3.1. exactly one section must come out, carrying both services. */
  ck_assert_int_eq(count_pid(0x0011), 1);
  pkt = first_pkt(0x0011);
  ck_assert(pkt != NULL);

  p = psi_new();
  psi_enable_multi_program(p);
  slen = psi_build_pat_multi(1, 0, entries, 2, sec, sizeof sec);
  wrap_ts_packet(patpkt, 0x0000, sec, slen);
  psi_feed(p, patpkt);
  psi_feed(p, pkt); /* real mpts_tick output, not a re-derived expectation */

  ck_assert_int_eq(psi_have_sdt(p), 1);
  progs = psi_multi_programs(p, &count);
  ck_assert_int_eq(count, 2);
  ck_assert_str_eq(progs[0].service_name, "svc");
  ck_assert_str_eq(progs[1].service_name, "svc");

  psi_free(p);
  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_bumps_sdt_version_only_when_active_set_changes) {
  psi_pat_entry_t entries[2] = {{101, 0x1000}, {102, 0x1001}};
  mpts_t *m = mpts_new(1, 1, "", entries, 2, &mock_program_ops);
  mock_program_t prog0 = {101, 0}, prog1 = {102, 0};
  unsigned v1, v2, v3;

  mpts_set_program(m, 0, &prog0);
  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);
  v1 = sdt_version(first_pkt(0x0011));

  g_count = 0;
  mpts_tick(m, 2.0, capture_cb, NULL); /* resend, active set unchanged: same version */
  v2 = sdt_version(first_pkt(0x0011));
  ck_assert_uint_eq(v2, v1);

  mpts_set_program(m, 1, &prog1); /* active set changed: content of the sub_table changed */
  g_count = 0;
  mpts_tick(m, 2.05, capture_cb, NULL); /* forced immediate resend, interval not otherwise due */
  v3 = sdt_version(first_pkt(0x0011));
  ck_assert_uint_eq(v3, (v1 + 1) & 0x1F);

  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_emits_cat_ecm_emm_via_cas_ops) {
  psi_pat_entry_t entries[1] = {{101, 0x1000}};
  mpts_t *m = mpts_new(1, 1, "", entries, 1, &mock_program_ops);
  mock_cas_t cas = {1, 2}; /* ecm due once, two emms queued */
  mpts_cas_vendor_pid_t vendors[1] = {{0x0020, 0x0021}};

  mpts_set_cas(m, &cas, &mock_cas_ops, vendors, 1);

  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);

  ck_assert(saw_pid(0x0001));  /* CAT */
  ck_assert(saw_pid(0x0020));  /* ECM */
  ck_assert_int_eq(count_pid(0x0021), 2); /* both queued EMMs drained in one tick */

  mpts_free(m);
}
END_TEST

START_TEST(mpts_tick_without_cas_emits_no_cat_ecm_emm) {
  psi_pat_entry_t entries[1] = {{101, 0x1000}};
  mpts_t *m = mpts_new(1, 1, "", entries, 1, &mock_program_ops);

  g_count = 0;
  mpts_tick(m, 0.0, capture_cb, NULL);

  ck_assert(!saw_pid(0x0001));
  ck_assert(!saw_pid(0x0020));
  ck_assert(!saw_pid(0x0021));

  mpts_free(m);
}
END_TEST

static Suite *mpts_suite(void) {
  Suite *s = suite_create("mpts");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mpts_tick_emits_multi_program_pat);
  tcase_add_test(tc, mpts_tick_omits_nit_when_no_network_name);
  tcase_add_test(tc, mpts_tick_emits_nit_when_network_name_set);
  tcase_add_test(tc, mpts_tick_skips_sdt_eit_for_inactive_program_but_keeps_pat_entry);
  tcase_add_test(tc, mpts_tick_shortly_after_sends_nothing_new);
  tcase_add_test(tc, mpts_tick_resends_eit_immediately_on_metadata_change);
  tcase_add_test(tc, mpts_tick_emits_one_composite_sdt_section_for_all_active_programs);
  tcase_add_test(tc, mpts_tick_bumps_sdt_version_only_when_active_set_changes);
  tcase_add_test(tc, mpts_tick_emits_cat_ecm_emm_via_cas_ops);
  tcase_add_test(tc, mpts_tick_without_cas_emits_no_cat_ecm_emm);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mpts_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
