/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/cas/biss/biss.h"

/* EBU Tech 3292 v3.0 Annex A worked example */
static const unsigned char annex_a_id[BISS_KEY_LEN] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
static const unsigned char annex_a_sw[BISS_KEY_LEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static const unsigned char annex_a_esw[BISS_KEY_LEN] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};

START_TEST(parse_hex16_accepts_lowercase) {
  unsigned char out[BISS_KEY_LEN];
  ck_assert_int_eq(biss_parse_hex16("000102030405060708090a0b0c0d0e0f", out), 0);
  ck_assert_mem_eq(out, annex_a_id, BISS_KEY_LEN);
}
END_TEST

START_TEST(parse_hex16_accepts_uppercase_and_0x_prefix) {
  unsigned char out[BISS_KEY_LEN];
  ck_assert_int_eq(biss_parse_hex16("0X00112233445566778899AABBCCDDEEFF", out), 0);
  ck_assert_mem_eq(out, annex_a_sw, BISS_KEY_LEN);
}
END_TEST

START_TEST(parse_hex16_rejects_wrong_length) {
  unsigned char out[BISS_KEY_LEN];
  ck_assert_int_eq(biss_parse_hex16("0011223344", out), -1);
  ck_assert_int_eq(biss_parse_hex16("00112233445566778899aabbccddeeff00", out), -1);
}
END_TEST

START_TEST(parse_hex16_rejects_non_hex_chars) {
  unsigned char out[BISS_KEY_LEN];
  ck_assert_int_eq(biss_parse_hex16("0011223344556677889gaabbccddeeff", out), -1);
}
END_TEST

START_TEST(esw_encrypt_matches_ebu_annex_a_vector) {
  unsigned char esw[BISS_KEY_LEN];
  ck_assert_int_eq(biss_esw_encrypt(annex_a_id, annex_a_sw, esw), 0);
  ck_assert_mem_eq(esw, annex_a_esw, BISS_KEY_LEN);
}
END_TEST

START_TEST(esw_decrypt_matches_ebu_annex_a_vector) {
  unsigned char sw[BISS_KEY_LEN];
  ck_assert_int_eq(biss_esw_decrypt(annex_a_id, annex_a_esw, sw), 0);
  ck_assert_mem_eq(sw, annex_a_sw, BISS_KEY_LEN);
}
END_TEST

START_TEST(esw_round_trips_with_arbitrary_key) {
  unsigned char id[BISS_KEY_LEN] = {0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  unsigned char sw[BISS_KEY_LEN] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  unsigned char esw[BISS_KEY_LEN], back[BISS_KEY_LEN];
  ck_assert_int_eq(biss_esw_encrypt(id, sw, esw), 0);
  ck_assert_mem_ne(esw, sw, BISS_KEY_LEN);
  ck_assert_int_eq(biss_esw_decrypt(id, esw, back), 0);
  ck_assert_mem_eq(back, sw, BISS_KEY_LEN);
}
END_TEST

START_TEST(biss1_parse_sw_computes_checksum_bytes) {
  unsigned char cw[BISS1_KEY_LEN];
  static const unsigned char expect[BISS1_KEY_LEN] = {0x01, 0x23, 0x45, 0x69, 0x67, 0x89, 0xab, 0x9b};
  ck_assert_int_eq(biss1_parse_sw("0123456789AB", cw), 0);
  ck_assert_mem_eq(cw, expect, BISS1_KEY_LEN);
}
END_TEST

START_TEST(biss1_parse_sw_accepts_0x_prefix) {
  unsigned char cw[BISS1_KEY_LEN];
  ck_assert_int_eq(biss1_parse_sw("0x0123456789ab", cw), 0);
  ck_assert_uint_eq(cw[0], 0x01);
  ck_assert_uint_eq(cw[3], 0x69);
}
END_TEST

START_TEST(biss1_parse_sw_rejects_wrong_length) {
  unsigned char cw[BISS1_KEY_LEN];
  ck_assert_int_eq(biss1_parse_sw("0123456789abcd", cw), -1); /* 14 chars, BISS2-shaped garbage */
  ck_assert_int_eq(biss1_parse_sw("01234567", cw), -1);
}
END_TEST

START_TEST(biss1_parse_sw_rejects_non_hex_chars) {
  unsigned char cw[BISS1_KEY_LEN];
  ck_assert_int_eq(biss1_parse_sw("0123456789zz", cw), -1);
}
END_TEST

/* EBU Tech 3292 rev. 2 Tables A.1, A.2. id: 14 hex, sw: expected clear SW */
typedef struct {
  const char *esw;
  const char *id;
  const char *sw;
  int buried;
} biss1_vector_t;

static const biss1_vector_t biss1_vectors[] = {
  {"E81816B87E5CF9C4", "FB5F9C585DD359", "6651DC93B1FC", 0},
  {"FE0E810E96648C43", "DC91106F13C87B", "5406386BE70D", 0},
  {"51B276C10BA89683", "89F27FB3ACA107", "0F1050049893", 0},
  {"1D599C25C30C2C11", "85513F83A5FF36", "17A7A594057F", 0},
  {"ED89E0D818B43B98", "CD23BF7D8F6BC3", "086220EE2993", 0},
  {"DA457923D39BDB81", "E0A48CAA093AC7", "FF00C8306C91", 0},
  {"389C30A2AB29F3F2", "FB5F9C585DD359", "6651DC93B1FC", 1},
  {"288871F187278E6A", "DC91106F13C87B", "5406386BE70D", 1},
  {"53C0C292CC2A88B3", "89F27FB3ACA107", "0F1050049893", 1},
  {"6EC275343C161742", "85513F83A5FF36", "17A7A594057F", 1},
  {"0E027A2DBDB5622C", "CD23BF7D8F6BC3", "086220EE2993", 1},
  {"C5E9F325179F97A2", "E0A48CAA093AC7", "FF00C8306C91", 1},
};

START_TEST(biss1_esw_decrypt_matches_ebu_appendix_a_vector) {
  const biss1_vector_t *v = &biss1_vectors[_i];
  unsigned char esw[BISS1_ESW_LEN], id[BISS1_ID_LEN], want[BISS1_KEY_LEN], got[BISS1_KEY_LEN];
  ck_assert_int_eq(biss1_parse_esw(v->esw, esw), 0);
  ck_assert_int_eq(biss1_parse_id(v->id, id), 0);
  ck_assert_int_eq(biss1_parse_sw(v->sw, want), 0);
  ck_assert_int_eq(biss1_esw_decrypt(id, esw, v->buried, got), 0);
  ck_assert_mem_eq(got, want, BISS1_KEY_LEN);
}
END_TEST

START_TEST(biss1_esw_round_trips_for_each_id_kind) {
  const biss1_vector_t *v = &biss1_vectors[_i];
  unsigned char id[BISS1_ID_LEN], cw[BISS1_KEY_LEN], esw[BISS1_ESW_LEN], back[BISS1_KEY_LEN];
  ck_assert_int_eq(biss1_parse_id(v->id, id), 0);
  ck_assert_int_eq(biss1_parse_sw(v->sw, cw), 0);
  ck_assert_int_eq(biss1_esw_encrypt(id, cw, v->buried, esw), 0);
  ck_assert_int_eq(biss1_esw_decrypt(id, esw, v->buried, back), 0);
  ck_assert_mem_eq(back, cw, BISS1_KEY_LEN);
}
END_TEST

START_TEST(biss1_esw_encrypt_fills_outer_bits_randomly) {
  unsigned char id[BISS1_ID_LEN], cw[BISS1_KEY_LEN], a[BISS1_ESW_LEN], b[BISS1_ESW_LEN];
  ck_assert_int_eq(biss1_parse_id("FB5F9C585DD359", id), 0);
  ck_assert_int_eq(biss1_parse_sw("6651DC93B1FC", cw), 0);
  ck_assert_int_eq(biss1_esw_encrypt(id, cw, 0, a), 0);
  ck_assert_int_eq(biss1_esw_encrypt(id, cw, 0, b), 0);
  ck_assert_mem_ne(a, b, BISS1_ESW_LEN);
}
END_TEST

START_TEST(biss1_esw_rejects_null_args) {
  unsigned char id[BISS1_ID_LEN] = {0}, buf[BISS1_KEY_LEN] = {0};
  ck_assert_int_eq(biss1_esw_decrypt(NULL, buf, 0, buf), -1);
  ck_assert_int_eq(biss1_esw_decrypt(id, NULL, 0, buf), -1);
  ck_assert_int_eq(biss1_esw_decrypt(id, buf, 0, NULL), -1);
  ck_assert_int_eq(biss1_esw_encrypt(NULL, buf, 0, buf), -1);
  ck_assert_int_eq(biss1_esw_encrypt(id, NULL, 0, buf), -1);
  ck_assert_int_eq(biss1_esw_encrypt(id, buf, 0, NULL), -1);
}
END_TEST

START_TEST(biss1_parse_id_accepts_0x_prefix_and_rejects_bad_input) {
  static const unsigned char expect[BISS1_ID_LEN] = {0xF0, 0x9A, 0x42, 0x3F, 0x56, 0x73, 0x8A};
  unsigned char id[BISS1_ID_LEN];
  ck_assert_int_eq(biss1_parse_id("0xF09A423F56738A", id), 0);
  ck_assert_mem_eq(id, expect, BISS1_ID_LEN);
  ck_assert_int_eq(biss1_parse_id("F09A423F5673", id), -1);
  ck_assert_int_eq(biss1_parse_id("F09A423F56738AFF", id), -1);
  ck_assert_int_eq(biss1_parse_id("F09A423F56738Z", id), -1);
}
END_TEST

START_TEST(biss1_parse_esw_accepts_0x_prefix_and_rejects_bad_input) {
  static const unsigned char expect[BISS1_ESW_LEN] = {0xF7, 0x6E, 0xE2, 0x49, 0xBE, 0x01, 0xA2, 0x86};
  unsigned char esw[BISS1_ESW_LEN];
  ck_assert_int_eq(biss1_parse_esw("0xF76EE249BE01A286", esw), 0);
  ck_assert_mem_eq(esw, expect, BISS1_ESW_LEN);
  ck_assert_int_eq(biss1_parse_esw("F76EE249BE01A2", esw), -1);
  ck_assert_int_eq(biss1_parse_esw("F76EE249BE01A2Z6", esw), -1);
}
END_TEST

static Suite *biss_suite(void) {
  Suite *s = suite_create("biss");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_hex16_accepts_lowercase);
  tcase_add_test(tc, parse_hex16_accepts_uppercase_and_0x_prefix);
  tcase_add_test(tc, parse_hex16_rejects_wrong_length);
  tcase_add_test(tc, parse_hex16_rejects_non_hex_chars);
  tcase_add_test(tc, esw_encrypt_matches_ebu_annex_a_vector);
  tcase_add_test(tc, esw_decrypt_matches_ebu_annex_a_vector);
  tcase_add_test(tc, esw_round_trips_with_arbitrary_key);
  tcase_add_test(tc, biss1_parse_sw_computes_checksum_bytes);
  tcase_add_test(tc, biss1_parse_sw_accepts_0x_prefix);
  tcase_add_test(tc, biss1_parse_sw_rejects_wrong_length);
  tcase_add_test(tc, biss1_parse_sw_rejects_non_hex_chars);
  tcase_add_loop_test(tc, biss1_esw_decrypt_matches_ebu_appendix_a_vector, 0, (int)(sizeof biss1_vectors / sizeof biss1_vectors[0]));
  tcase_add_loop_test(tc, biss1_esw_round_trips_for_each_id_kind, 0, (int)(sizeof biss1_vectors / sizeof biss1_vectors[0]));
  tcase_add_test(tc, biss1_esw_encrypt_fills_outer_bits_randomly);
  tcase_add_test(tc, biss1_esw_rejects_null_args);
  tcase_add_test(tc, biss1_parse_id_accepts_0x_prefix_and_rejects_bad_input);
  tcase_add_test(tc, biss1_parse_esw_accepts_0x_prefix_and_rejects_bad_input);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(biss_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
