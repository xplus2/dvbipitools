/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/index.h"

typedef struct {
  bim_index_cont_t *c;
  int n;
  uint32_t nfrags; /* fragments 1..nfrags exist in data containers 1..2 */
} fake_t;

static int f_find(void *ctx, unsigned container, unsigned type, unsigned id, const unsigned char **body, size_t *len) {
  const fake_t *f = ctx;
  for (int i = 0; i < f->n; i++) {
    if (f->c[i].id != container) continue;
    for (int j = 0; j < f->c[i].nstructs; j++)
      if (f->c[i].structs[j].type == type && f->c[i].structs[j].id == id) {
        *body = f->c[i].structs[j].body;
        *len = f->c[i].structs[j].len;
        return 0;
      }
  }
  return -1;
}

static int f_has(void *ctx, unsigned container, uint32_t fragment) {
  const fake_t *f = ctx;
  return (container == 1 || container == 2) && fragment >= 1 && fragment <= f->nfrags;
}

START_TEST(index_names_parse) {
  unsigned m;
  ck_assert_int_eq(bim_index_parse_names("pi-crid,schedule-time", &m), 0);
  ck_assert_uint_eq(m, 1u << BIM_INDEX_PI_CRID | 1u << BIM_INDEX_SCHED_TIME);
  ck_assert_int_eq(bim_index_parse_names("pi-crid,,pi-title", &m), -1);
  ck_assert_int_eq(bim_index_parse_names("group-crid", &m), -1);
}
END_TEST

static void fill(bim_index_input_t in[BIM_INDEX_KINDS], int n, char keys[][2][40]) {
  memset(in, 0, sizeof *in * BIM_INDEX_KINDS);
  for (int i = 0; i < n; i++) {
    /* reverse order: build sorts */
    snprintf(keys[i][0], 40, "crid://x/%04d", n - i);
    snprintf(keys[i][1], 40, "svc%d", i % 3);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_CRID], keys[i][0], NULL, (uint16_t)(1 + i % 2), (uint32_t)(1 + i)), 0);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_TITLE], i % 2 ? "Same" : keys[i][0] + 9, NULL, 1, (uint32_t)(1 + i)), 0);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_SCHED_TIME], i % 2 ? "2030-01-01T12:00:00Z" : "2030-01-01T13:00:00+02:00", keys[i][1], 2, (uint32_t)(1 + i)), 0);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_SCHED_TITLE], keys[i][1], NULL, 2, (uint32_t)(1 + i)), 0);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_GI_CRID], keys[i][0], NULL, 1, (uint32_t)(1 + i)), 0);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_GI_TITLE], keys[i][1], NULL, 2, (uint32_t)(1 + i)), 0);
  }
}

static void free_in(bim_index_input_t in[BIM_INDEX_KINDS]) {
  for (int k = 0; k < BIM_INDEX_KINDS; k++) bim_index_input_free(&in[k]);
}

START_TEST(index_build_then_check_clean) {
  static char keys[40][2][40];
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_report_t r;
  fake_t f = {NULL, 0, 40};
  fill(in, 40, keys);
  ck_assert_int_eq(bim_index_build((1u << BIM_INDEX_KINDS) - 1, in, 3, &f.c, &f.n), 0);
  ck_assert_int_eq(f.n, 1 + BIM_INDEX_KINDS);
  ck_assert_uint_eq(f.c[0].id, BIM_INDEX_LIST_CONTAINER);
  ck_assert_uint_eq(f.c[1].id, 3);
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), 0);
  ck_assert_int_eq(r.indexes, BIM_INDEX_KINDS);
  ck_assert_int_eq(r.entries, BIM_INDEX_KINDS * 40);
  ck_assert_int_eq(r.ignored + r.missing + r.malformed + r.misordered + r.unresolved, 0);
  bim_index_conts_free(f.c, f.n);
  free_in(in);
}
END_TEST

START_TEST(index_check_reports_unresolved_and_missing) {
  static char keys[8][2][40];
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_report_t r;
  fake_t f = {NULL, 0, 4};
  fill(in, 8, keys);
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_PI_CRID | 1u << BIM_INDEX_SCHED_TIME, in, 3, &f.c, &f.n), 0);
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), 0);
  ck_assert_int_eq(r.indexes, 2);
  ck_assert_int_eq(r.unresolved, 2 * 4);
  f.c[2].id = 99; /* sub-index container of 2nd index not received */
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), 0);
  ck_assert_int_eq(r.missing, 1);
  bim_index_conts_free(f.c, f.n);
  free_in(in);
}
END_TEST

START_TEST(index_check_needs_list_and_flags_disorder) {
  static char keys[4][2][40];
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_report_t r;
  fake_t f = {NULL, 0, 4};
  fill(in, 4, keys);
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_PI_CRID, in, 3, &f.c, &f.n), 0);
  for (int j = 0; j < f.c[1].nstructs; j++) {
    bim_struct_t *s = &f.c[1].structs[j];
    if (s->type != BIM_STRUCT_SUB_INDEX) continue;
    /* swap 1st and 2nd entry */
    unsigned char t[7];
    memcpy(t, s->body + 1, 7);
    memcpy(s->body + 1, s->body + 8, 7);
    memcpy(s->body + 8, t, 7);
  }
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), 0);
  ck_assert_int_gt(r.misordered, 0);
  f.c[0].id = 7;
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), -1);
  bim_index_conts_free(f.c, f.n);
  free_in(in);
}
END_TEST

START_TEST(index_chunks_at_string_limit) {
  static char big[300][2][300];
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_report_t r;
  fake_t f = {NULL, 0, 300};
  memset(in, 0, sizeof in);
  for (int i = 0; i < 300; i++) {
    memset(big[i][0], 'a' + i % 26, 250);
    snprintf(big[i][0] + 250, 40, "%04d", i);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_TITLE], big[i][0], NULL, 1, (uint32_t)(1 + i)), 0);
  }
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_PI_TITLE, in, 3, &f.c, &f.n), 0);
  ck_assert_int_gt(f.n, 2);
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), 0);
  ck_assert_int_eq(r.entries, 300);
  ck_assert_int_eq(r.missing + r.malformed + r.misordered + r.unresolved, 0);
  bim_index_conts_free(f.c, f.n);
  free_in(in);
}
END_TEST

/* TS 102 323 table 105: type list structure, fragment_type 0xFFFF carries an XPath string */
START_TEST(typelist_round_trips) {
  static const uint16_t c1[] = {1, 2, 0x1234};
  static const uint16_t c2[] = {7};
  bim_typelist_entry_t in[] = {{0x0001, NULL, c1, 3}, {0xFFFF, "/tva:TVAMain/tva:Foo", c2, 1}, {0x0005, NULL, NULL, 0}};
  static const unsigned char want[] = {0x00, 0x03, 0xF0, 0x09, 0x00, 0x01, 0x03, 0x00, 0x01, 0x00, 0x02, 0x12, 0x34, 0xF0, 0x07, 0xFF, 0xFF, 0x00, 0x01, 0x01, 0x00, 0x07, 0xF0, 0x03, 0x00, 0x05, 0x00};
  strrepo_writer_t sw;
  unsigned char *body;
  size_t len;
  bim_typelist_entry_t *out;
  int n;
  const unsigned char *strs;
  size_t slen;
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_typelist_build(in, 3, &sw, &body, &len), 0);
  ck_assert_uint_eq(len, sizeof want);
  ck_assert_mem_eq(body, want, len);
  strs = strrepo_writer_data(&sw, &slen);
  ck_assert_int_eq(bim_typelist_parse(body, len, strs, slen, &out, &n), 0);
  ck_assert_int_eq(n, 3);
  ck_assert_uint_eq(out[0].fragment_type, 1);
  ck_assert_int_eq(out[0].ncontainers, 3);
  ck_assert_uint_eq(out[0].containers[2], 0x1234);
  ck_assert_str_eq(out[1].xpath, "/tva:TVAMain/tva:Foo");
  ck_assert_int_eq(out[2].ncontainers, 0);
  bim_typelist_free(out, n);
  ck_assert_int_eq(bim_typelist_parse(body, len - 1, strs, slen, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(body, len, NULL, 0, &out, &n), -1);
  free(body);
  strrepo_writer_free(&sw);
}
END_TEST

/* table 106: namespace map structure */
START_TEST(nsmap_round_trips) {
  static const char *const px[] = {"tva2", "int"};
  static const char *const ns[] = {"urn:tva:metadata:extended:2007", "urn:tva:metadata:int:2007"};
  unsigned char *body;
  size_t len;
  char **p;
  char **u;
  int n;
  ck_assert_int_eq(bim_nsmap_build(px, ns, 2, &body, &len), 0);
  ck_assert_uint_eq(body[0], 2);
  ck_assert_uint_eq(body[1], 4);
  ck_assert_int_eq(bim_nsmap_parse(body, len, &p, &u, &n), 0);
  ck_assert_int_eq(n, 2);
  ck_assert_str_eq(p[1], "int");
  ck_assert_str_eq(u[0], ns[0]);
  bim_nsmap_free(p, u, n);
  ck_assert_int_eq(bim_nsmap_parse(body, len - 1, &p, &u, &n), -1);
  ck_assert_ptr_null(p);
  free(body);
  {
    char big[300];
    const char *bp[1] = {big};
    memset(big, 'a', sizeof big);
    big[299] = '\0';
    ck_assert_int_eq(bim_nsmap_build(bp, ns, 1, &body, &len), -1);
  }
}
END_TEST

static bim_struct_t *sfind(fake_t *f, unsigned cont, unsigned type, unsigned id) {
  for (int i = 0; i < f->n; i++) {
    if (f->c[i].id != cont) continue;
    for (int j = 0; j < f->c[i].nstructs; j++)
      if (f->c[i].structs[j].type == type && f->c[i].structs[j].id == id) return &f->c[i].structs[j];
  }
  ck_abort_msg("structure %u/%u/%u absent", cont, type, id);
  return NULL;
}

static void put16be(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

static unsigned get16be(const unsigned char *p) { return (unsigned)(p[0] << 8 | p[1]); }

typedef void (*mut_fn)(fake_t *f);

static bim_index_report_t mutated(unsigned mask, int n, mut_fn mut) {
  static char keys[40][2][40];
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_report_t r;
  fake_t f = {NULL, 0, 40};
  fill(in, n, keys);
  ck_assert_int_eq(bim_index_build(mask, in, 3, &f.c, &f.n), 0);
  if (mut) mut(&f);
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), 0);
  bim_index_conts_free(f.c, f.n);
  free_in(in);
  return r;
}

#define SUB(f) sfind(f, 3, BIM_STRUCT_SUB_INDEX, 0)
#define IDX(f, k) sfind(f, 0, BIM_STRUCT_INDEX, (unsigned)(k))

static void m_sub_absent(fake_t *f) { SUB(f)->type = 0x99; }
static void m_sub_strings_absent(fake_t *f) { sfind(f, 3, BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_STRINGS)->type = 0x99; }
static void m_sub_header(fake_t *f) { SUB(f)->body[0] = 0x00; }
static void m_sub_short(fake_t *f) { SUB(f)->len--; }
static void m_sub_empty(fake_t *f) { SUB(f)->len = 0; }
static void m_sub_bad_string(fake_t *f) { put16be(SUB(f)->body + 8, 0xFFFF); }
static void m_idx_hi_low(fake_t *f) {
  bim_struct_t *i = IDX(f, BIM_INDEX_PI_CRID);
  put16be(i->body + 2, get16be(SUB(f)->body + 1));
}
static void m_idx_absent(fake_t *f) { IDX(f, BIM_INDEX_PI_CRID)->type = 0x99; }
static void m_idx_tiny(fake_t *f) { IDX(f, BIM_INDEX_PI_CRID)->len = 1; }
static void m_idx_locator(fake_t *f) { IDX(f, BIM_INDEX_PI_CRID)->body[1] = 0; }
static void m_idx_stride(fake_t *f) { IDX(f, BIM_INDEX_PI_CRID)->len--; }
static void m_idx_bad_hi(fake_t *f) { put16be(IDX(f, BIM_INDEX_PI_CRID)->body + 2, 0xFFFF); }
static void m_idx_overlap(fake_t *f) {
  bim_struct_t *i = IDX(f, BIM_INDEX_PI_CRID);
  unsigned hi = get16be(i->body + 2);
  unsigned char *nb = malloc(9);
  ck_assert_ptr_nonnull(nb);
  nb[0] = 0xBF;
  nb[1] = 1;
  put16be(nb + 2, hi);
  put16be(nb + 4, hi);
  put16be(nb + 6, 3);
  nb[8] = 0;
  free(i->body);
  i->body = nb;
  i->len = 9;
}

START_TEST(index_check_flags_single_layer_damage) {
  bim_index_report_t r = mutated(1u << BIM_INDEX_PI_CRID, 4, NULL);
  ck_assert_int_eq(r.missing + r.malformed + r.misordered, 0);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_sub_absent);
  ck_assert_int_eq(r.missing, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_sub_strings_absent);
  ck_assert_int_eq(r.missing, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_sub_header);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_sub_short);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_sub_empty);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_sub_bad_string);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_idx_hi_low);
  ck_assert_int_gt(r.misordered, 0);
}
END_TEST

START_TEST(index_check_flags_index_structure_damage) {
  bim_index_report_t r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_idx_absent);
  ck_assert_int_eq(r.missing, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_idx_tiny);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_idx_locator);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_idx_stride);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_idx_bad_hi);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(1u << BIM_INDEX_PI_CRID, 4, m_idx_overlap);
  ck_assert_int_eq(r.indexes, 1);
}
END_TEST

#define L1(f) sfind(f, 3, BIM_STRUCT_SUB_INDEX, 0)
#define L2(f) sfind(f, 3, BIM_STRUCT_SUB_INDEX, 1)

static void m_l1_tiny(fake_t *f) { L1(f)->len = 1; }
static void m_l1_header(fake_t *f) { L1(f)->body[0] = 0xFF; }
static void m_l1_stride(fake_t *f) { L1(f)->len--; }
static void m_l1_link(fake_t *f) { L1(f)->body[1] = 77; }
static void m_l2_header(fake_t *f) { L2(f)->body[0] = 0; }
static void m_l2_short(fake_t *f) { L2(f)->len--; }
static void m_l1_end(fake_t *f) { put16be(L1(f)->body + 2, 0xFFFF); }
static void m_l1_last(fake_t *f) { put16be(L1(f)->body + 4, 0xFFFF); }
static void m_l2_key(fake_t *f) { put16be(L2(f)->body + 1, 0xFFFF); }
static void m_l2_swap(fake_t *f) {
  unsigned char t[7];
  memcpy(t, L2(f)->body + 1, 7);
  memcpy(L2(f)->body + 1, L2(f)->body + 8, 7);
  memcpy(L2(f)->body + 8, t, 7);
}
static void m_l1_trailing(fake_t *f) {
  bim_struct_t *l = L1(f);
  unsigned last = get16be(l->body + l->len - 2);
  put16be(l->body + l->len - 2, last - 1);
}
static void m_l2_strings_absent(fake_t *f) { sfind(f, 3, BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_STRINGS)->type = 0x99; }

START_TEST(index_check_flags_two_layer_damage) {
  unsigned mask = 1u << BIM_INDEX_SCHED_TIME;
  bim_index_report_t r = mutated(mask, 6, NULL);
  ck_assert_int_eq(r.missing + r.malformed + r.misordered, 0);
  r = mutated(mask, 6, m_l1_tiny);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l1_header);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l1_stride);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l1_link);
  ck_assert_int_eq(r.missing, 1);
  r = mutated(mask, 6, m_l2_header);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l2_short);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l1_end);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l1_last);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l2_key);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l2_swap);
  ck_assert_int_gt(r.misordered, 0);
  r = mutated(mask, 6, m_l1_trailing);
  ck_assert_int_eq(r.malformed, 1);
  r = mutated(mask, 6, m_l2_strings_absent);
  ck_assert_int_eq(r.missing, 1);
}
END_TEST

static int raw_find(void *ctx, unsigned container, unsigned type, unsigned id, const unsigned char **body, size_t *len) {
  const fake_t *f = ctx;
  return f_find((void *)f, container, type, id, body, len);
}

static bim_index_report_t raw_list(const unsigned char *list, size_t llen, const unsigned char *strs, size_t slen) {
  bim_struct_t st[2];
  bim_index_cont_t c;
  fake_t f;
  bim_index_report_t r;
  st[0] = (bim_struct_t){BIM_STRUCT_INDEX_LIST, BIM_STRUCT_ID_UNUSED, (unsigned char *)list, llen};
  st[1] = (bim_struct_t){BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_STRINGS, (unsigned char *)strs, slen};
  c.id = BIM_INDEX_LIST_CONTAINER;
  c.structs = st;
  c.nstructs = strs ? 2 : 1;
  f.c = &c;
  f.n = 1;
  f.nfrags = 0;
  ck_assert_int_eq(bim_index_check(raw_find, f_has, &f, &r), 0);
  return r;
}

START_TEST(index_check_reads_hand_made_lists) {
  static const unsigned char strs[] = {0, 'a', '@', 't', 'v', 'a', ':', 'x', 0};
  bim_index_report_t r;
  /* dl below 3 */
  static const unsigned char l1[] = {0x01, 0xFF};
  /* entry longer than the list */
  static const unsigned char l2[] = {0x20, 0x00, 0x01, 0x00};
  /* ftype 0xFFFF with xpath offset, no fields */
  static const unsigned char l3[] = {0x05, 0xFF, 0xFF, 0x00, 0x01, 0x00};
  /* nf 0 */
  static const unsigned char l4[] = {0x03, 0x00, 0x01, 0x00};
  /* fid-coded pi-title entry, index absent */
  static const unsigned char l5[] = {0x0A, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01};
  /* xpath field, string repository present but offset invalid */
  static const unsigned char l6[] = {0x0C, 0x00, 0x01, 0x01, 0xFF, 0xFF, 0x00, 0x77, 0x00, 0x00, 0x00, 0x03, 0x01};
  /* nf 2, fields cut off */
  static const unsigned char l7[] = {0x06, 0x00, 0x05, 0x02, 0x00, 0x04, 0x00};
  /* xpath field cut after marker */
  static const unsigned char l8[] = {0x06, 0x00, 0x05, 0x01, 0xFF, 0xFF, 0x00};
  /* xpath ftype and xpath field with string */
  static const unsigned char l9[] = {0x0E, 0xFF, 0xFF, 0x00, 0x01, 0x01, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01};
  r = raw_list(l1, sizeof l1, strs, sizeof strs);
  ck_assert_int_eq(r.malformed, 1);
  r = raw_list(l2, sizeof l2, strs, sizeof strs);
  ck_assert_int_eq(r.malformed, 1);
  r = raw_list(l3, sizeof l3, strs, sizeof strs);
  ck_assert_int_eq(r.ignored, 1);
  r = raw_list(l4, sizeof l4, NULL, 0);
  ck_assert_int_eq(r.ignored, 1);
  r = raw_list(l5, sizeof l5, strs, sizeof strs);
  ck_assert_int_eq(r.indexes, 1);
  ck_assert_int_eq(r.missing, 1);
  r = raw_list(l6, sizeof l6, strs, sizeof strs);
  ck_assert_int_eq(r.ignored, 1);
  r = raw_list(l6, sizeof l6, NULL, 0);
  ck_assert_int_eq(r.ignored, 1);
  r = raw_list(l7, sizeof l7, strs, sizeof strs);
  ck_assert_int_eq(r.ignored, 1);
  r = raw_list(l8, sizeof l8, strs, sizeof strs);
  ck_assert_int_eq(r.ignored, 1);
  r = raw_list(l9, sizeof l9, strs, sizeof strs);
  ck_assert_int_eq(r.ignored, 1);
}
END_TEST

START_TEST(index_build_limits) {
  static char key[3000];
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_cont_t *c = NULL;
  int n = 0;
  char *big;
  memset(in, 0, sizeof in);
  memset(key, 'k', sizeof key - 1);
  for (int i = 0; i < 1500; i++) {
    char *k = malloc(sizeof key);
    ck_assert_ptr_nonnull(k);
    memcpy(k, key, sizeof key);
    snprintf(k, 12, "%08d", i);
    k[8] = 'k';
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_TITLE], k, NULL, 1, (uint32_t)(1 + i)), 0);
  }
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_PI_TITLE, in, 3, &c, &n), -2);
  ck_assert_ptr_null(c);
  for (int i = 0; i < in[BIM_INDEX_PI_TITLE].n; i++) free((char *)in[BIM_INDEX_PI_TITLE].e[i].key[0]);
  bim_index_input_free(&in[BIM_INDEX_PI_TITLE]);

  for (int i = 0; i < 300; i++) {
    char *k = malloc(300);
    ck_assert_ptr_nonnull(k);
    memset(k, 'a' + i % 26, 299);
    k[299] = 0;
    k[0] = (char)('a' + i / 100 % 26);
    k[1] = (char)('a' + i / 10 % 10);
    k[2] = (char)('a' + i % 10);
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_TITLE], k, NULL, 1, (uint32_t)(1 + i)), 0);
  }
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_PI_TITLE, in, 0xFFFF, &c, &n), -3);
  for (int i = 0; i < in[BIM_INDEX_PI_TITLE].n; i++) free((char *)in[BIM_INDEX_PI_TITLE].e[i].key[0]);
  bim_index_input_free(&in[BIM_INDEX_PI_TITLE]);

  big = malloc(70000);
  ck_assert_ptr_nonnull(big);
  memset(big, 'x', 69999);
  big[69999] = 0;
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_TITLE], big, NULL, 1, 1), 0);
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_PI_TITLE, in, 3, &c, &n), -1);
  free(big);
  bim_index_input_free(&in[BIM_INDEX_PI_TITLE]);
}
END_TEST

START_TEST(index_build_allocation_failures) {
  static char keys[8][2][40];
  for (long n = 0;; n++) {
    bim_index_input_t in[BIM_INDEX_KINDS];
    bim_index_cont_t *c = NULL;
    int nc = 0;
    int hit;
    int rc;
    fill(in, 8, keys);
    alloc_fail_at(n);
    rc = bim_index_build((1u << BIM_INDEX_KINDS) - 1, in, 3, &c, &nc);
    hit = alloc_fail_end();
    if (rc == 0) bim_index_conts_free(c, nc);
    else ck_assert_ptr_null(c);
    free_in(in);
    if (!hit) break;
  }
  for (long n = 0; n < 40; n++) {
    bim_index_input_t in = {NULL, 0, 0};
    alloc_fail_at(n);
    (void)bim_index_add(&in, "a", NULL, 1, 1);
    (void)bim_index_add(&in, "b", NULL, 1, 2);
    alloc_fail_end();
    bim_index_input_free(&in);
  }
}
END_TEST

START_TEST(index_sort_keys_with_equal_instants_and_containers) {
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_cont_t *c = NULL;
  int n = 0;
  bim_index_report_t r;
  fake_t f;
  memset(in, 0, sizeof in);
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_SCHED_TIME], "2030-01-01T12:00:00Z", "s1", 2, 1), 0);
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_SCHED_TIME], "2030-01-01T14:00:00+02:00", "s1", 1, 2), 0);
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_SCHED_TIME], "not a time", "s1", 1, 3), 0);
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_SCHED_TIME], "not a time", "s2", 1, 3), 0);
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_CRID], "same", NULL, 2, 5), 0);
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_CRID], "same", NULL, 1, 5), 0);
  ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_CRID], "same", NULL, 1, 4), 0);
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_SCHED_TIME | 1u << BIM_INDEX_PI_CRID, in, 3, &c, &n), 0);
  f.c = c;
  f.n = n;
  f.nfrags = 9;
  ck_assert_int_eq(bim_index_check(f_find, f_has, &f, &r), 0);
  ck_assert_int_eq(r.misordered + r.malformed + r.missing, 0);
  bim_index_conts_free(c, n);
  free_in(in);
}
END_TEST

START_TEST(typelist_build_rejects_invalid) {
  static const uint16_t cs[1] = {1};
  uint16_t many[256];
  strrepo_writer_t sw;
  unsigned char *body = NULL;
  size_t len;
  char *big = malloc(70000);
  bim_typelist_entry_t e[1];
  ck_assert_ptr_nonnull(big);
  memset(big, 'x', 69999);
  big[69999] = 0;
  memset(many, 0, sizeof many);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  e[0] = (bim_typelist_entry_t){1, NULL, cs, 1};
  ck_assert_int_eq(bim_typelist_build(e, -1, &sw, &body, &len), -1);
  ck_assert_int_eq(bim_typelist_build(e, 0x10000, &sw, &body, &len), -1);
  e[0].ncontainers = -1;
  ck_assert_int_eq(bim_typelist_build(e, 1, &sw, &body, &len), -1);
  e[0].ncontainers = 256;
  e[0].containers = many;
  ck_assert_int_eq(bim_typelist_build(e, 1, &sw, &body, &len), -1);
  e[0] = (bim_typelist_entry_t){0x10000, NULL, cs, 1};
  ck_assert_int_eq(bim_typelist_build(e, 1, &sw, &body, &len), -1);
  e[0] = (bim_typelist_entry_t){0xFFFF, NULL, cs, 1};
  ck_assert_int_eq(bim_typelist_build(e, 1, &sw, &body, &len), -1);
  e[0] = (bim_typelist_entry_t){0xFFFF, big, cs, 1};
  ck_assert_int_eq(bim_typelist_build(e, 1, &sw, &body, &len), -1);
  strrepo_writer_free(&sw);
  free(big);
}
END_TEST

START_TEST(typelist_parse_rejects_malformed) {
  static const unsigned char strs[] = {0, 'x', 0};
  static const unsigned char noterm[] = {0, 'x'};
  bim_typelist_entry_t *out;
  int n;
  /* count 1 with nothing after */
  static const unsigned char b1[] = {0x00, 0x01};
  /* dl beyond the body */
  static const unsigned char b2[] = {0x00, 0x01, 0xF0, 0x09, 0x00};
  /* dl below 3 */
  static const unsigned char b3[] = {0x00, 0x01, 0xF0, 0x02, 0x00, 0x01};
  /* xpath type with dl below 5 */
  static const unsigned char b4[] = {0x00, 0x01, 0xF0, 0x03, 0xFF, 0xFF, 0x00};
  /* xpath offset beyond strings */
  static const unsigned char b5[] = {0x00, 0x01, 0xF0, 0x05, 0xFF, 0xFF, 0x00, 0x09, 0x00};
  /* container count mismatches dl */
  static const unsigned char b6[] = {0x00, 0x01, 0xF0, 0x04, 0x00, 0x01, 0x02, 0x00};
  /* trailing byte */
  static const unsigned char b7[] = {0x00, 0x01, 0xF0, 0x03, 0x00, 0x01, 0x00, 0x77};
  static const unsigned char ok[] = {0x00, 0x01, 0xF0, 0x05, 0xFF, 0xFF, 0x00, 0x01, 0x00};
  ck_assert_int_eq(bim_typelist_parse(b1, 1, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(b1, sizeof b1, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(b2, sizeof b2, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(b3, sizeof b3, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(b4, sizeof b4, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(b5, sizeof b5, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(b6, sizeof b6, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(b7, sizeof b7, strs, sizeof strs, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(ok, sizeof ok, noterm, sizeof noterm, &out, &n), -1);
  ck_assert_int_eq(bim_typelist_parse(ok, sizeof ok, strs, sizeof strs, &out, &n), 0);
  bim_typelist_free(out, n);
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_typelist_parse(ok, sizeof ok, strs, sizeof strs, &out, &n);
    hit = alloc_fail_end();
    if (!rc) bim_typelist_free(out, n);
    if (!hit) break;
  }
  {
    static const unsigned char two[] = {0x00, 0x01, 0xF0, 0x07, 0x00, 0x01, 0x02, 0x00, 0x03, 0x00, 0x04};
    for (long k = 0;; k++) {
      int hit;
      int rc;
      alloc_fail_at(k);
      rc = bim_typelist_parse(two, sizeof two, strs, sizeof strs, &out, &n);
      hit = alloc_fail_end();
      if (!rc) bim_typelist_free(out, n);
      if (!hit) break;
    }
  }
}
END_TEST

START_TEST(nsmap_rejects_malformed_and_allocation_failures) {
  static const char *const px[] = {"a", "bb"};
  static const char *const ns[] = {"urn:a", "urn:bb"};
  char big[300];
  const char *bp[1] = {big};
  const char *bn[1] = {"u"};
  unsigned char *body;
  size_t len;
  char **p;
  char **u;
  int n;
  /* prefix length beyond body */
  static const unsigned char s1[] = {0x01, 0x09, 'a'};
  /* ns length beyond body */
  static const unsigned char s2[] = {0x01, 0x01, 'a', 0x09, 'u'};
  /* trailing byte */
  static const unsigned char s3[] = {0x01, 0x01, 'a', 0x01, 'u', 0x00};
  /* count with no entries */
  static const unsigned char s4[] = {0x02};
  memset(big, 'a', sizeof big);
  big[299] = 0;
  ck_assert_int_eq(bim_nsmap_parse(s1, 0, &p, &u, &n), -1);
  ck_assert_int_eq(bim_nsmap_parse(s1, sizeof s1, &p, &u, &n), -1);
  ck_assert_int_eq(bim_nsmap_parse(s2, sizeof s2, &p, &u, &n), -1);
  ck_assert_int_eq(bim_nsmap_parse(s3, sizeof s3, &p, &u, &n), -1);
  ck_assert_int_eq(bim_nsmap_parse(s4, sizeof s4, &p, &u, &n), -1);
  ck_assert_int_eq(bim_nsmap_build(px, ns, -1, &body, &len), -1);
  ck_assert_int_eq(bim_nsmap_build(px, ns, 300, &body, &len), -1);
  ck_assert_int_eq(bim_nsmap_build(bp, bn, 1, &body, &len), -1);
  ck_assert_int_eq(bim_nsmap_build(bn, bp, 1, &body, &len), -1);
  ck_assert_int_eq(bim_nsmap_build(px, ns, 2, &body, &len), 0);
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_nsmap_parse(body, len, &p, &u, &n);
    hit = alloc_fail_end();
    if (!rc) bim_nsmap_free(p, u, n);
    if (!hit) break;
  }
  for (long k = 0;; k++) {
    unsigned char *b2 = NULL;
    size_t l2;
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_nsmap_build(px, ns, 2, &b2, &l2);
    hit = alloc_fail_end();
    if (!rc) free(b2);
    if (!hit) break;
  }
  for (long k = 0;; k++) {
    strrepo_writer_t sw;
    static const uint16_t cs[2] = {1, 2};
    bim_typelist_entry_t e[2] = {{1, NULL, cs, 2}, {0xFFFF, "/a/b", cs, 1}};
    unsigned char *b2 = NULL;
    size_t l2;
    int hit;
    int rc;
    ck_assert_int_eq(strrepo_writer_init(&sw), 0);
    alloc_fail_at(k);
    rc = bim_typelist_build(e, 2, &sw, &b2, &l2);
    hit = alloc_fail_end();
    if (!rc) free(b2);
    strrepo_writer_free(&sw);
    if (!hit) break;
  }
  free(body);
}
END_TEST

static Suite *index_suite(void) {
  Suite *s = suite_create("bim_index");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, index_names_parse);
  tcase_add_test(tc, index_build_then_check_clean);
  tcase_add_test(tc, index_check_reports_unresolved_and_missing);
  tcase_add_test(tc, index_check_needs_list_and_flags_disorder);
  tcase_add_test(tc, index_chunks_at_string_limit);
  tcase_add_test(tc, typelist_round_trips);
  tcase_add_test(tc, nsmap_round_trips);
  tcase_add_test(tc, index_check_flags_single_layer_damage);
  tcase_add_test(tc, index_check_flags_index_structure_damage);
  tcase_add_test(tc, index_check_flags_two_layer_damage);
  tcase_add_test(tc, index_check_reads_hand_made_lists);
  tcase_add_test(tc, index_build_limits);
  tcase_add_test(tc, index_build_allocation_failures);
  tcase_add_test(tc, index_sort_keys_with_equal_instants_and_containers);
  tcase_add_test(tc, typelist_build_rejects_invalid);
  tcase_add_test(tc, typelist_parse_rejects_malformed);
  tcase_add_test(tc, nsmap_rejects_malformed_and_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(index_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
