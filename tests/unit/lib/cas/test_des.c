/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/des.h"

typedef struct {
  unsigned char key[DES_BLOCK_LEN];
  unsigned char pt[DES_BLOCK_LEN];
  unsigned char ct[DES_BLOCK_LEN];
} des_vector_t;

/* FIPS 81 example, classic worked example, NIST SP 800-17 variable plaintext/key */
static const des_vector_t vectors[] = {
  {{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF}, {0x4E, 0x6F, 0x77, 0x20, 0x69, 0x73, 0x20, 0x74}, {0x3F, 0xA4, 0x0E, 0x8A, 0x98, 0x4D, 0x48, 0x15}},
  {{0x13, 0x34, 0x57, 0x79, 0x9B, 0xBC, 0xDF, 0xF1}, {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF}, {0x85, 0xE8, 0x13, 0x54, 0x0F, 0x0A, 0xB4, 0x05}},
  {{0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01}, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x95, 0xF8, 0xA5, 0xE5, 0xDD, 0x31, 0xD9, 0x00}},
  {{0x80, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x95, 0xA8, 0xD7, 0x28, 0x13, 0xDA, 0xA9, 0x4D}},
};

START_TEST(encrypt_matches_known_answer) {
  const des_vector_t *v = &vectors[_i];
  des_key_t ks;
  unsigned char out[DES_BLOCK_LEN];
  des_set_key(&ks, v->key);
  des_encrypt(&ks, v->pt, out);
  ck_assert_mem_eq(out, v->ct, DES_BLOCK_LEN);
}
END_TEST

START_TEST(decrypt_matches_known_answer) {
  const des_vector_t *v = &vectors[_i];
  des_key_t ks;
  unsigned char out[DES_BLOCK_LEN];
  des_set_key(&ks, v->key);
  des_decrypt(&ks, v->ct, out);
  ck_assert_mem_eq(out, v->pt, DES_BLOCK_LEN);
}
END_TEST

START_TEST(parity_bits_do_not_change_the_key_schedule) {
  const des_vector_t *v = &vectors[_i];
  unsigned char flipped[DES_BLOCK_LEN];
  des_key_t a, b;
  for (int i = 0; i < DES_BLOCK_LEN; i++) flipped[i] = v->key[i] ^ 1;
  des_set_key(&a, v->key);
  des_set_key(&b, flipped);
  ck_assert_mem_eq(&a, &b, sizeof a);
}
END_TEST

START_TEST(in_and_out_may_alias) {
  const des_vector_t *v = &vectors[0];
  des_key_t ks;
  unsigned char buf[DES_BLOCK_LEN];
  des_set_key(&ks, v->key);
  memcpy(buf, v->pt, sizeof buf);
  des_encrypt(&ks, buf, buf);
  ck_assert_mem_eq(buf, v->ct, DES_BLOCK_LEN);
  des_decrypt(&ks, buf, buf);
  ck_assert_mem_eq(buf, v->pt, DES_BLOCK_LEN);
}
END_TEST

START_TEST(des3_with_equal_keys_is_single_des) {
  const des_vector_t *v = &vectors[_i];
  des_key_t ks;
  unsigned char out[DES_BLOCK_LEN];
  des_set_key(&ks, v->key);
  des3_decrypt(&ks, &ks, &ks, v->ct, out);
  ck_assert_mem_eq(out, v->pt, DES_BLOCK_LEN);
}
END_TEST

/* cross-checked against OpenSSL DES-EDE3-ECB */
START_TEST(des3_decrypt_matches_known_answer) {
  static const unsigned char k1[DES_BLOCK_LEN] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  static const unsigned char k2[DES_BLOCK_LEN] = {0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01};
  static const unsigned char k3[DES_BLOCK_LEN] = {0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23};
  static const unsigned char pt[DES_BLOCK_LEN] = {0x54, 0x68, 0x65, 0x20, 0x71, 0x75, 0x66, 0x63};
  static const unsigned char ct[DES_BLOCK_LEN] = {0xA8, 0x26, 0xFD, 0x8C, 0xE5, 0x3B, 0x85, 0x5F};
  des_key_t a, b, c;
  unsigned char out[DES_BLOCK_LEN];
  des_set_key(&a, k1);
  des_set_key(&b, k2);
  des_set_key(&c, k3);
  des3_decrypt(&a, &b, &c, ct, out);
  ck_assert_mem_eq(out, pt, DES_BLOCK_LEN);
}
END_TEST

START_TEST(key_expand_sets_odd_parity) {
  static const unsigned char in[DES_KEY56_LEN] = {0x4D, 0xA1, 0x9F, 0xF0, 0xAF, 0x6B, 0x8F};
  static const unsigned char id[DES_KEY56_LEN] = {0xFB, 0x5F, 0x9C, 0x58, 0x5D, 0xD3, 0x59};
  static const unsigned char id_mapped[DES_BLOCK_LEN] = {0xFB, 0xAE, 0xE6, 0x8A, 0x85, 0xEF, 0x4C, 0xB3};
  unsigned char out[DES_BLOCK_LEN];
  des_key_expand(in, out);
  for (int i = 0; i < DES_BLOCK_LEN; i++) ck_assert_int_eq(__builtin_parity(out[i]), 1);
  des_key_expand(id, out);
  ck_assert_mem_eq(out, id_mapped, DES_BLOCK_LEN);
}
END_TEST

static Suite *des_suite(void) {
  Suite *s = suite_create("des");
  TCase *tc = tcase_create("core");
  int n = (int)(sizeof vectors / sizeof vectors[0]);
  tcase_add_loop_test(tc, encrypt_matches_known_answer, 0, n);
  tcase_add_loop_test(tc, decrypt_matches_known_answer, 0, n);
  tcase_add_loop_test(tc, parity_bits_do_not_change_the_key_schedule, 0, n);
  tcase_add_test(tc, in_and_out_may_alias);
  tcase_add_loop_test(tc, des3_with_equal_keys_is_single_des, 0, n);
  tcase_add_test(tc, des3_decrypt_matches_known_answer);
  tcase_add_test(tc, key_expand_sets_odd_parity);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(des_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
