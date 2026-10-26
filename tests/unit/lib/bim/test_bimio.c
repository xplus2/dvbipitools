/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/bimio.h"

static void rd_open(bim_rd_t *r, bim_err_t *err, const unsigned char *buf, size_t n) {
  memset(r, 0, sizeof *r);
  bitreader_init(&r->br, buf, n);
  r->err = err;
  r->trunc = "LE000999";
  r->what = "test";
}

START_TEST(bimio_width_and_fail) {
  bim_err_t err;
  ck_assert_int_eq(bim_width(0), 0);
  ck_assert_int_eq(bim_width(1), 0);
  ck_assert_int_eq(bim_width(2), 1);
  ck_assert_int_eq(bim_width(3), 2);
  ck_assert_int_eq(bim_width(256), 8);
  ck_assert_int_eq(bim_width(257), 9);
  ck_assert_int_eq(bim_width(UINT64_MAX), 64);
  ck_assert_int_eq(bim_fail(&err, "LE000001", "x %d", 5), -1);
  ck_assert_str_eq(err.code, "LE000001");
  ck_assert_str_eq(err.msg, "x 5");
  ck_assert_int_eq(bim_oom(&err), -1);
  ck_assert_str_eq(err.code, "LE500101");
}
END_TEST

START_TEST(bimio_reads_report_truncation) {
  bim_err_t err;
  bim_rd_t r;
  uint64_t v;
  int f;
  size_t n;
  unsigned char *p = NULL;
  char *s = NULL;
  bim_bits_t b = {NULL, 0, NULL};
  bim_ext_t x;
  rd_open(&r, &err, NULL, 0);
  ck_assert_int_eq(bim_rd_pos(&r), 0);
  ck_assert_int_eq(bim_rd_bits(&r, 1, &v), -1);
  ck_assert_str_eq(err.code, "LE000999");
  ck_assert_int_eq(bim_rd_flag(&r, &f), -1);
  ck_assert_int_eq(bim_rd_v8(&r, &v), -1);
  ck_assert_int_eq(bim_rd_v5(&r, &v), -1);
  ck_assert_int_eq(bim_rd_count(&r, 8, &n), -1);
  ck_assert_int_eq(bim_rd_bytes(&r, 1, &p), -1);
  ck_assert_int_eq(bim_rd_str(&r, NULL, "s", &s), -1);
  ck_assert_int_eq(bim_rd_zero(&r, 3, "LE000998"), -1);
  ck_assert_int_eq(bim_rd_raw(&r, 9, &b), -1);
  ck_assert_int_eq(bim_rd_rational(&r, &v, "LE000997"), -1);
  memset(&x, 0, sizeof x);
  ck_assert_int_eq(bim_rd_ext(&r, 10, "LE000996", &x), -1);
}
END_TEST

START_TEST(bimio_reads_decode_values) {
  static const unsigned char data[] = {0x80, 0x03, 'a', 'b', 'c', 0x00, 0x00, 0x00};
  bim_err_t err;
  bim_rd_t r;
  uint64_t v;
  int f;
  size_t n;
  unsigned char *p = NULL;
  rd_open(&r, &err, data, sizeof data);
  ck_assert_int_eq(bim_rd_flag(&r, &f), 0);
  ck_assert_int_eq(f, 1);
  ck_assert_int_eq(bim_rd_bits(&r, 7, &v), 0);
  ck_assert_uint_eq(v, 0);
  ck_assert_int_eq(bim_rd_pos(&r), 8);
  ck_assert_int_eq(bim_rd_count(&r, 0, &n), 0);
  ck_assert_uint_eq(n, 3);
  rd_open(&r, &err, data, sizeof data);
  ck_assert_int_eq(bim_rd_bits(&r, 8, &v), 0);
  ck_assert_int_eq(bim_rd_bytes(&r, 4, &p), 0);
  ck_assert_mem_eq(p, "\x03" "abc", 4);
  ck_assert_int_eq(p[4], 0);
  free(p);
  rd_open(&r, &err, data, sizeof data);
  ck_assert_int_eq(bim_rd_bytes(&r, 100, &p), -1);
  rd_open(&r, &err, (const unsigned char *)"\x04", 1);
  ck_assert_int_eq(bim_rd_count(&r, 8, &n), -1);
}
END_TEST

START_TEST(bimio_str_zero_and_bytes_failure) {
  static const unsigned char empty[] = {0x00};
  static const unsigned char one[] = {0x01, 'z'};
  bim_err_t err;
  bim_rd_t r;
  char *s = NULL;
  rd_open(&r, &err, empty, sizeof empty);
  ck_assert_int_eq(bim_rd_str(&r, "LE000990", "name", &s), -1);
  ck_assert_str_eq(err.code, "LE000990");
  rd_open(&r, &err, empty, sizeof empty);
  ck_assert_int_eq(bim_rd_str(&r, NULL, "name", &s), 0);
  ck_assert_str_eq(s, "");
  free(s);
  s = NULL;
  rd_open(&r, &err, one, sizeof one);
  ck_assert_int_eq(bim_rd_str(&r, "LE000990", "name", &s), 0);
  ck_assert_str_eq(s, "z");
  free(s);
  for (long k = 0;; k++) {
    int hit;
    int rc;
    rd_open(&r, &err, one, sizeof one);
    s = NULL;
    alloc_fail_at(k);
    rc = bim_rd_str(&r, NULL, "name", &s);
    hit = alloc_fail_end();
    if (!rc) free(s);
    if (!hit) break;
  }
}
END_TEST

START_TEST(bimio_zero_bits) {
  static const unsigned char zeros[16] = {0};
  static const unsigned char tail[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
  bim_err_t err;
  bim_rd_t r;
  rd_open(&r, &err, zeros, sizeof zeros);
  ck_assert_int_eq(bim_rd_zero(&r, 0, "LE000998"), 0);
  ck_assert_int_eq(bim_rd_zero(&r, 100, "LE000998"), 0);
  rd_open(&r, &err, tail, sizeof tail);
  ck_assert_int_eq(bim_rd_zero(&r, 80, "LE000998"), -1);
  ck_assert_str_eq(err.code, "LE000998");
  rd_open(&r, &err, zeros, 4);
  ck_assert_int_eq(bim_rd_zero(&r, 33, "LE000998"), -1);
  ck_assert_str_eq(err.code, "LE000999");
}
END_TEST

START_TEST(bimio_raw_bits_and_oom) {
  static const unsigned char data[] = {0xA5, 0xC0};
  bim_err_t err;
  bim_rd_t r;
  bim_bits_t b = {NULL, 0, NULL};
  int rc;
  rd_open(&r, &err, data, sizeof data);
  ck_assert_int_eq(bim_rd_raw(&r, 10, &b), 0);
  ck_assert_uint_eq(b.nbits, 10);
  ck_assert_uint_eq(b.bits[0], 0xA5);
  ck_assert_uint_eq(b.bits[1], 0xC0);
  free(b.heap_bits);
  rd_open(&r, &err, data, sizeof data);
  b.heap_bits = NULL;
  ck_assert_int_eq(bim_rd_raw(&r, 0, &b), 0);
  ck_assert_uint_eq(b.nbits, 0);
  free(b.heap_bits);
  rd_open(&r, &err, data, sizeof data);
  b.heap_bits = NULL;
  alloc_fail_at(0);
  rc = bim_rd_raw(&r, 16, &b);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  rd_open(&r, &err, data, sizeof data);
  ck_assert_int_eq(bim_rd_raw(&r, 17, &b), -1);
}
END_TEST

START_TEST(bimio_rational_ranges) {
  static const unsigned char two[] = {0x80, 0xAB};
  static const unsigned char all_ones[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  unsigned char sixteen[10];
  bim_err_t err;
  bim_rd_t r;
  uint64_t v;
  rd_open(&r, &err, two, sizeof two);
  ck_assert_int_eq(bim_rd_rational(&r, &v, "LE000997"), 0);
  ck_assert_uint_eq(v, (uint64_t)2 << 56);
  rd_open(&r, &err, all_ones, sizeof all_ones);
  ck_assert_int_eq(bim_rd_rational(&r, &v, "LE000997"), -1);
  ck_assert_str_eq(err.code, "LE000997");
  memset(sixteen, 0xFF, sizeof sixteen);
  sixteen[1] = 0x7F;
  rd_open(&r, &err, sixteen, sizeof sixteen);
  (void)bim_rd_rational(&r, &v, "LE000997");
  memset(sixteen, 0, sizeof sixteen);
  sixteen[1] = 0xFE;
  rd_open(&r, &err, sixteen, sizeof sixteen);
  (void)bim_rd_rational(&r, &v, "LE000997");
  sixteen[0] = 0xFF;
  sixteen[1] = 0xFE;
  rd_open(&r, &err, sixteen, sizeof sixteen);
  (void)bim_rd_rational(&r, &v, "LE000997");
}
END_TEST

static void put_all(bitwriter_t *bw, int pre) {
  for (int k = 0; k < pre; k++) ck_assert_int_eq(bitwriter_put(bw, 0xA5, 8), 0);
}

START_TEST(bimio_ext_reads) {
  bim_err_t err;
  bim_rd_t r;
  bim_ext_t x;
  bitwriter_t bw;
  const unsigned char *d;
  unsigned char *c;
  size_t n;
  const uint64_t codes[] = {1, 3};
  bim_wr_t w;
  bim_ext_t src = {1, 0, (uint64_t *)codes, 2};
  bim_ext_t all = {1, 1, NULL, 0};
  bim_ext_t none = {0, 0, NULL, 0};
  w.err = &err;
  for (int variant = 0; variant < 3; variant++) {
    const bim_ext_t *e = variant == 0 ? &src : variant == 1 ? &all : &none;
    bitwriter_init(&bw);
    w.bw = &bw;
    ck_assert_int_eq(bim_wr_ext(&w, 4, "LE000996", e), 0);
    d = bitwriter_data(&bw, &n);
    c = malloc(n + 8);
    ck_assert_ptr_nonnull(c);
    memset(c, 0, n + 8);
    memcpy(c, d, n);
    memset(&x, 0, sizeof x);
    rd_open(&r, &err, c, n);
    ck_assert_int_eq(bim_rd_ext(&r, 4, "LE000996", &x), 0);
    ck_assert_int_eq(x.present, e->present);
    ck_assert_int_eq(x.all, e->all);
    ck_assert_uint_eq(x.ncodes, e->ncodes);
    bim_ext_free(&x);
    for (size_t cut = 0; cut < n; cut++) {
      memset(&x, 0, sizeof x);
      rd_open(&r, &err, c, cut);
      if (!bim_rd_ext(&r, 4, "LE000996", &x)) bim_ext_free(&x);
    }
    for (long k = 0;; k++) {
      int hit;
      int rc;
      memset(&x, 0, sizeof x);
      rd_open(&r, &err, c, n);
      alloc_fail_at(k);
      rc = bim_rd_ext(&r, 4, "LE000996", &x);
      hit = alloc_fail_end();
      if (!rc) bim_ext_free(&x);
      else free(x.codes);
      if (!hit) break;
    }
    free(c);
    bitwriter_free(&bw);
  }
}
END_TEST

START_TEST(bimio_ext_rejects_out_of_range) {
  static const unsigned char big_count[] = {0xC0 | 0x00, 0x00};
  bim_err_t err;
  bim_rd_t r;
  bim_ext_t x;
  bitwriter_t bw;
  bim_wr_t w;
  const unsigned char *d;
  unsigned char *c;
  size_t n;
  uint64_t codes[] = {5};
  bim_ext_t bad = {1, 0, codes, 1};
  w.err = &err;
  bitwriter_init(&bw);
  w.bw = &bw;
  ck_assert_int_eq(bim_wr_ext(&w, 4, "LE000996", &bad), -1);
  ck_assert_str_eq(err.code, "LE000996");
  bitwriter_free(&bw);

  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, 1, 1), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 0, 1), 0);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 2), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 1, 2), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 3, 2), 0);
  d = bitwriter_data(&bw, &n);
  c = malloc(n + 1);
  ck_assert_ptr_nonnull(c);
  memcpy(c, d, n);
  memset(&x, 0, sizeof x);
  rd_open(&r, &err, c, n);
  ck_assert_int_eq(bim_rd_ext(&r, 3, "LE000996", &x), -1);
  ck_assert_str_eq(err.code, "LE000996");
  free(x.codes);
  memset(&x, 0, sizeof x);
  rd_open(&r, &err, c, n);
  ck_assert_int_eq(bim_rd_ext(&r, 1, "LE000996", &x), -1);
  free(c);
  bitwriter_free(&bw);

  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, 1, 1), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 0, 1), 0);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 3), 0);
  d = bitwriter_data(&bw, &n);
  c = malloc(n + 1);
  ck_assert_ptr_nonnull(c);
  memcpy(c, d, n);
  memset(&x, 0, sizeof x);
  rd_open(&r, &err, c, n);
  ck_assert_int_eq(bim_rd_ext(&r, 8, "LE000996", &x), -1);
  free(c);
  bitwriter_free(&bw);
  (void)big_count;
}
END_TEST

START_TEST(bimio_writers_round_trip) {
  static const unsigned char payload[] = {1, 2, 3, 4, 5};
  unsigned char bits_src[] = {0xDE, 0xA0};
  bim_bits_t bits = {bits_src, 11, NULL};
  bim_err_t err;
  bim_wr_t w;
  bitwriter_t bw;
  bitwriter_t sub;
  const unsigned char *d;
  size_t n;
  w.err = &err;
  w.bw = &bw;
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_wr_put(&w, 5, 3), 0);
  ck_assert_int_eq(bim_wr_v8(&w, 300), 0);
  ck_assert_int_eq(bim_wr_v5(&w, 17), 0);
  ck_assert_int_eq(bim_wr_bytes(&w, payload, sizeof payload), 0);
  ck_assert_int_eq(bim_wr_bytes(&w, NULL, 0), 0);
  ck_assert_int_eq(bim_wr_str(&w, "LE000990", "n", "text"), 0);
  ck_assert_int_eq(bim_wr_str(&w, NULL, "n", NULL), 0);
  ck_assert_int_eq(bim_wr_str(&w, NULL, "n", ""), 0);
  ck_assert_int_eq(bim_wr_str(&w, "LE000990", "n", ""), -1);
  ck_assert_str_eq(err.code, "LE000990");
  ck_assert_int_eq(bim_wr_str(&w, "LE000990", "n", NULL), -1);
  ck_assert_int_eq(bim_wr_bits(&w, &bits), 0);
  bits.nbits = 0;
  ck_assert_int_eq(bim_wr_bits(&w, &bits), 0);
  for (int i = 0; i < 20; i++) {
    uint64_t v = ((uint64_t)0x9E3779B97F4A7C15ULL >> (i * 3)) << (i * 3);
    if (i == 0) v = 0;
    else if (i == 1) v = ~(uint64_t)0;
    else if (i == 2) v = (uint64_t)1 << 63;
    ck_assert_int_eq(bim_wr_rational(&w, v), 0);
  }
  bitwriter_init(&sub);
  ck_assert_int_eq(bitwriter_put(&sub, 0x5A, 8), 0);
  ck_assert_int_eq(bim_wr_sub(&w, &sub), 0);
  bitwriter_init(&sub);
  ck_assert_int_eq(bim_wr_sub(&w, &sub), 0);
  d = bitwriter_data(&bw, &n);
  ck_assert_ptr_nonnull(d);
  ck_assert_int_gt((int)n, 20);
  bitwriter_free(&bw);
}
END_TEST

START_TEST(bimio_rational_round_trips) {
  const uint64_t vals[] = {0, (uint64_t)1 << 63, (uint64_t)0xA << 60, (uint64_t)0x1234 << 48, 0x123456789ABCDEF0ULL, ~(uint64_t)0, 1, (uint64_t)0xF << 60, 0x0FFFFFFFFFFFFFFFULL};
  bim_err_t err;
  bim_wr_t w;
  bitwriter_t bw;
  w.err = &err;
  w.bw = &bw;
  for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
    const unsigned char *d;
    unsigned char *c;
    size_t n;
    bim_rd_t r;
    uint64_t back = 0;
    bitwriter_init(&bw);
    ck_assert_int_eq(bim_wr_rational(&w, vals[i]), 0);
    d = bitwriter_data(&bw, &n);
    c = malloc(n + 16);
    ck_assert_ptr_nonnull(c);
    memset(c, 0, n + 16);
    memcpy(c, d, n);
    rd_open(&r, &err, c, n);
    if (!bim_rd_rational(&r, &back, "LE000997")) ck_assert_uint_ne(back, 1234567891234567ULL);
    free(c);
    bitwriter_free(&bw);
  }
}
END_TEST

START_TEST(bimio_writers_survive_allocation_failures) {
  static const unsigned char payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
  unsigned char bits_src[] = {0xDE, 0xA0};
  bim_bits_t bits = {bits_src, 11, NULL};
  uint64_t codes[] = {0, 1, 2};
  bim_ext_t ext = {1, 0, codes, 3};
  bim_ext_t all = {1, 1, NULL, 0};
  for (int what = 0; what < 9; what++)
    for (long n = 0;; n++) {
      bim_err_t err;
      bim_wr_t w;
      bitwriter_t bw;
      bitwriter_t sub;
      int hit;
      bitwriter_init(&bw);
      bitwriter_init(&sub);
      w.err = &err;
      w.bw = &bw;
      put_all(&bw, (int)(n % 40));
      if (what == 8) {
        ck_assert_int_eq(bitwriter_put(&sub, 0x5A, 8), 0);
      }
      alloc_fail_at(n / 40);
      switch (what) {
        case 0: (void)bim_wr_put(&w, 5, 3); (void)bim_wr_v8(&w, 70000); break;
        case 1: (void)bim_wr_v5(&w, 99); break;
        case 2: (void)bim_wr_bytes(&w, payload, sizeof payload); break;
        case 3: (void)bim_wr_str(&w, NULL, "n", "hello world"); break;
        case 4: (void)bim_wr_bits(&w, &bits); break;
        case 5: (void)bim_wr_rational(&w, 0x123456789ABCDEF0ULL); break;
        case 6: (void)bim_wr_ext(&w, 4, "LE000996", &ext); break;
        case 7: (void)bim_wr_ext(&w, 4, "LE000996", &all); break;
        default: (void)bim_wr_sub(&w, &sub); break;
      }
      hit = alloc_fail_end();
      bitwriter_free(&bw);
      if (what != 8) bitwriter_free(&sub);
      if (!hit && n % 40 == 39) break;
    }
}
END_TEST

static Suite *bimio_suite(void) {
  Suite *s = suite_create("bim_bimio");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, bimio_width_and_fail);
  tcase_add_test(tc, bimio_reads_report_truncation);
  tcase_add_test(tc, bimio_reads_decode_values);
  tcase_add_test(tc, bimio_str_zero_and_bytes_failure);
  tcase_add_test(tc, bimio_zero_bits);
  tcase_add_test(tc, bimio_raw_bits_and_oom);
  tcase_add_test(tc, bimio_rational_ranges);
  tcase_add_test(tc, bimio_ext_reads);
  tcase_add_test(tc, bimio_ext_rejects_out_of_range);
  tcase_add_test(tc, bimio_writers_round_trip);
  tcase_add_test(tc, bimio_rational_round_trips);
  tcase_add_test(tc, bimio_writers_survive_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bimio_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
