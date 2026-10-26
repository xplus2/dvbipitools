/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/codec/decode/priv.h"
#include "lib/bim/codec/laxsym.h"
#include "../../fx_bim.h"

typedef struct {
  bitwriter_t bw;
} sb_t;

static void sb_init(sb_t *s, int agg, int zlib) {
  bitwriter_init(&s->bw);
  ck_assert_int_eq(bitwriter_put(&s->bw, 0, 4) || bitwriter_put(&s->bw, 0, 4) || bitwriter_put(&s->bw, 0, 6) || bitwriter_put(&s->bw, (uint64_t)agg, 1) ||
                       bitwriter_put(&s->bw, (uint64_t)zlib, 1), 0);
}

static void sb_ev(sb_t *s, unsigned uid) { ck_assert_int_eq(bitwriter_put(&s->bw, 1, 1) || bitwriter_put(&s->bw, uid, 15), 0); }

static void sb_byte(sb_t *s, unsigned b) { ck_assert_int_eq(bitwriter_put(&s->bw, b, 8), 0); }

static void sb_str(sb_t *s, const char *v) {
  for (; *v; v++) sb_byte(s, (unsigned char)*v);
  sb_byte(s, 0);
}

static int never(const unsigned char *, size_t, unsigned char *, size_t *) { return -1; }

static int run(const unsigned char *bits, size_t n, bim_inflate_fn inflate, bim_node_t **out) {
  bitreader_t br;
  bim_decode_opts_t o;
  dec_t d;
  bim_node_t *node = bim_node_new("root");
  int rc;
  ck_assert_ptr_nonnull(node);
  memset(&o, 0, sizeof o);
  o.inflate = inflate;
  o.mpegb = 1;
  o.lax_schema = -1;
  bitreader_init(&br, bits, n);
  dec_init(&d, &o, &br, NULL);
  rc = dec_lax(&d, &br, n * 8, node);
  dec_done(&d);
  if (out) *out = node;
  else bim_node_free(node);
  return rc;
}

static int run_sb(sb_t *s, bim_inflate_fn inflate, bim_node_t **out) {
  size_t n;
  const unsigned char *p = bitwriter_data(&s->bw, &n);
  return run(p, n, inflate, out);
}

static void preamble(sb_t *s) {
  sb_ev(s, 1);
  sb_str(s, "urn:a");
  sb_ev(s, 1);
  sb_str(s, "");
  sb_ev(s, 2);
  sb_byte(s, 0);
  sb_str(s, "El");
  sb_ev(s, 3);
  sb_byte(s, 0);
  sb_str(s, "At");
  sb_ev(s, 3);
  sb_byte(s, 1);
  sb_str(s, "Bt");
}

static void vocabulary(sb_t *s, int agg, int zlib) {
  sb_init(s, agg, zlib);
  preamble(s);
  sb_ev(s, 24);
  sb_str(s, "attr value");
  sb_ev(s, 26);
  sb_str(s, "second");
  sb_ev(s, 23);
  sb_ev(s, 6);
  sb_str(s, "text");
  sb_ev(s, 23);
  sb_ev(s, 4);
  sb_ev(s, 4);
  sb_ev(s, 5);
  sb_byte(s, 0);
  sb_str(s, "x");
  sb_ev(s, 8);
  sb_str(s, "a");
  sb_str(s, "b");
  for (unsigned e = 9; e <= 22; e++) {
    int n = 0;
    if (e == 9 || e == 21 || e == 22 || e == 16) n = 1;
    else if (e == 10 || e == 14 || e == 19) n = 3;
    else if (e == 11) n = 4;
    else if (e == 12) n = 5;
    else if (e == 13 || e == 15) n = 2;
    sb_ev(s, e);
    for (int i = 0; i < n; i++) sb_str(s, "s");
  }
  sb_ev(s, 7);
}

START_TEST(lax_events_decode_every_event_kind) {
  for (int agg = 0; agg < 2; agg++)
    for (int zlib = 0; zlib < 2; zlib++) {
      sb_t s;
      bim_node_t *n = NULL;
      vocabulary(&s, agg, zlib);
      ck_assert_int_eq(run_sb(&s, ident, &n), 0);
      ck_assert_int_eq(n->nkids, 1);
      ck_assert_str_eq(n->kids[0]->name, "El");
      ck_assert_int_eq(n->kids[0]->nkids, 2);
      bim_node_free(n);
      bitwriter_free(&s.bw);
    }
}
END_TEST

static int fails(void (*build)(sb_t *s), bim_inflate_fn inflate) {
  sb_t s;
  int rc;
  sb_init(&s, 0, 0);
  build(&s);
  rc = run_sb(&s, inflate, NULL);
  bitwriter_free(&s.bw);
  return rc;
}

static void b_end0(sb_t *s) { sb_ev(s, 0); }
static void b_endel_top(sb_t *s) { sb_ev(s, 4); }
static void b_end_deep(sb_t *s) {
  preamble(s);
  sb_ev(s, 23);
  sb_ev(s, 7);
}
static void b_ns_range(sb_t *s) {
  sb_ev(s, 2);
  sb_byte(s, 5);
  sb_str(s, "x");
}
static void b_ns_range5(sb_t *s) {
  sb_ev(s, 5);
  sb_byte(s, 9);
  sb_str(s, "x");
}
static void b_el_missing(sb_t *s) { sb_ev(s, 23); }
static void b_at_missing(sb_t *s) {
  sb_ev(s, 24);
  sb_str(s, "v");
}
static void b_depth(sb_t *s) {
  preamble(s);
  for (int i = 0; i < 70; i++) sb_ev(s, 23);
}
static void b_pending(sb_t *s) {
  preamble(s);
  for (int i = 0; i < 70; i++) {
    sb_ev(s, 24);
    sb_str(s, "v");
  }
}
static void b_many_ns(sb_t *s) {
  for (int i = 0; i < 300; i++) {
    sb_ev(s, 1);
    sb_str(s, "u");
  }
}
static void b_long_string(sb_t *s) {
  sb_ev(s, 6);
  for (int i = 0; i < 70000; i++) sb_byte(s, 'a');
  sb_byte(s, 0);
}
static void b_nothing(sb_t *) {}
static void b_unterminated(sb_t *s) {
  sb_ev(s, 6);
  sb_byte(s, 'a');
}

START_TEST(lax_events_reject_malformed_streams) {
  ck_assert_int_eq(fails(b_end0, ident), -1);
  ck_assert_int_eq(fails(b_endel_top, ident), -1);
  ck_assert_int_eq(fails(b_end_deep, ident), -1);
  ck_assert_int_eq(fails(b_ns_range, ident), -1);
  ck_assert_int_eq(fails(b_ns_range5, ident), -1);
  ck_assert_int_eq(fails(b_el_missing, ident), -1);
  ck_assert_int_eq(fails(b_at_missing, ident), -1);
  ck_assert_int_eq(fails(b_depth, ident), -1);
  ck_assert_int_eq(fails(b_pending, ident), -1);
  ck_assert_int_eq(fails(b_many_ns, ident), -1);
  ck_assert_int_eq(fails(b_long_string, ident), -1);
  ck_assert_int_eq(fails(b_nothing, ident), -1);
  ck_assert_int_eq(fails(b_unterminated, ident), -1);
}
END_TEST

START_TEST(lax_header_and_compression_checks) {
  sb_t s;
  unsigned char buf[4];
  bim_node_t *n = NULL;
  memset(buf, 0, sizeof buf);
  ck_assert_int_eq(run(buf, 0, ident, NULL), -1);
  ck_assert_int_eq(run(buf, 1, ident, NULL), -1);
  buf[0] = 0x02;
  ck_assert_int_eq(run(buf, 2, ident, NULL), -1);
  buf[0] = 0x00;
  buf[1] = 0x01;
  ck_assert_int_eq(run(buf, 2, NULL, NULL), -1);
  buf[1] = 0x00;
  buf[0] = 0x10;
  (void)run(buf, 2, ident, NULL);
  vocabulary(&s, 0, 1);
  ck_assert_int_eq(run_sb(&s, NULL, NULL), -1);
  ck_assert_int_eq(run_sb(&s, never, NULL), -1);
  bitwriter_free(&s.bw);

  sb_init(&s, 0, 1);
  preamble(&s);
  for (int i = 0; i < 4000; i++) {
    sb_ev(&s, 6);
    sb_str(&s, "xyz");
  }
  sb_ev(&s, 7);
  ck_assert_int_eq(run_sb(&s, ident, &n), 0);
  ck_assert_int_eq(n->nkids, 4000);
  bim_node_free(n);
  bitwriter_free(&s.bw);
}
END_TEST

START_TEST(lax_events_sweeps_and_allocation_failures) {
  for (int agg = 0; agg < 2; agg++)
    for (int zlib = 0; zlib < 2; zlib++) {
      sb_t s;
      size_t n;
      unsigned char *copy;
      const unsigned char *p;
      vocabulary(&s, agg, zlib);
      p = bitwriter_data(&s.bw, &n);
      copy = malloc(n);
      ck_assert_ptr_nonnull(copy);
      memcpy(copy, p, n);
      for (size_t cut = 0; cut < n; cut++) (void)run(copy, cut, ident, NULL);
      for (size_t bit = 0; bit < n * 8; bit++) {
        copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
        (void)run(copy, n, ident, NULL);
        copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      }
      for (long k = 0;; k++) {
        bim_node_t *node = NULL;
        bitreader_t br;
        bim_decode_opts_t o;
        dec_t d;
        int hit;
        int rc;
        node = bim_node_new("root");
        memset(&o, 0, sizeof o);
        o.inflate = ident;
        o.mpegb = 1;
        o.lax_schema = -1;
        bitreader_init(&br, copy, n);
        dec_init(&d, &o, &br, NULL);
        alloc_fail_at(k);
        rc = dec_lax(&d, &br, n * 8, node);
        hit = alloc_fail_end();
        (void)rc;
        dec_done(&d);
        bim_node_free(node);
        if (!hit) break;
      }
      free(copy);
      bitwriter_free(&s.bw);
    }
}
END_TEST

START_TEST(lax_symbol_table_updates_and_evicts) {
  for (int agg = 0; agg < 2; agg++) {
    lax_syms_t t;
    const lax_sym_t *e;
    lax_syms_init(&t, agg);
    ck_assert_int_eq(lax_syms_code(&t, 5), 5);
    ck_assert_int_eq(lax_syms_code(&t, 5000), -1);
    ck_assert_ptr_nonnull(lax_syms_by_code(&t, 127));
    ck_assert_ptr_null(lax_syms_by_code(&t, 128));
    ck_assert_ptr_null(lax_syms_by_code(&t, -1));
    lax_syms_event(&t, 7);
    lax_syms_event(&t, 7);
    lax_syms_event(&t, 8);
    lax_syms_event(&t, 7);
    lax_syms_event(&t, 8);
    ck_assert_int_eq(lax_syms_code(&t, 7), lax_syms_by_code(&t, lax_syms_code(&t, 7))->code);
    e = lax_syms_by_code(&t, lax_syms_code(&t, 8));
    ck_assert_ptr_nonnull(e);
    for (uint32_t ev = 1000; ev < 1400; ev++) {
      lax_syms_event(&t, ev);
      lax_syms_event(&t, ev % 5);
    }
    ck_assert_int_ge(lax_syms_code(&t, 1399), 0);
    ck_assert_int_eq(lax_syms_code(&t, 1000), -1);
    for (int c = 0; c < LAX_SYMS; c++) ck_assert_ptr_nonnull(lax_syms_by_code(&t, c));
  }
}
END_TEST

static Suite *lax_suite(void) {
  Suite *s = suite_create("bim_lax_events");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, lax_events_decode_every_event_kind);
  tcase_add_test(tc, lax_events_reject_malformed_streams);
  tcase_add_test(tc, lax_header_and_compression_checks);
  tcase_add_test(tc, lax_events_sweeps_and_allocation_failures);
  tcase_add_test(tc, lax_symbol_table_updates_and_evicts);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(lax_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
