/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/cas/ecmg_client/cw_encryption.h"

static void hex_decode(const char *hex, unsigned char *out) {
  size_t n = strlen(hex) / 2;
  for (size_t i = 0; i < n; i++)
    sscanf(hex + 2 * i, "%2hhx", &out[i]);
}

START_TEST(des56_expand_matches_annex_d_kat) {
  unsigned char in[7], expect[8], out[8];
  hex_decode("4DA19FF0AF6B8F", in);
  hex_decode("4CD067FE0B7AAE1F", expect);
  cwenc_des56_expand(in, out);
  ck_assert_mem_eq(out, expect, 8);
}
END_TEST

START_TEST(des_ecb_encrypt_matches_known_vector) {
  unsigned char key[8], pt[8], expect[8], out[8];
  hex_decode("133457799BBCDFF1", key);
  hex_decode("0123456789ABCDEF", pt);
  hex_decode("85E813540F0AB405", expect);
  ck_assert_int_eq(cwenc_des_ecb_encrypt(key, pt, sizeof pt, out), 0);
  ck_assert_mem_eq(out, expect, 8);
}
END_TEST

START_TEST(des_ecb_encrypt_rejects_non_block_length) {
  unsigned char key[8] = {0}, pt[5] = {0}, out[8];
  ck_assert_int_eq(cwenc_des_ecb_encrypt(key, pt, sizeof pt, out), -1);
}
END_TEST

START_TEST(aes128_ecb_encrypt_matches_fips197) {
  unsigned char key[16], pt[16], expect[16], out[16];
  hex_decode("000102030405060708090a0b0c0d0e0f", key);
  hex_decode("00112233445566778899aabbccddeeff", pt);
  hex_decode("69c4e0d86a7b0430d8cdb78070b4c55a", expect);
  ck_assert_int_eq(cwenc_aes_ecb_encrypt(128, key, pt, out), 0);
  ck_assert_mem_eq(out, expect, 16);
}
END_TEST

START_TEST(aes256_ecb_encrypt_matches_fips197) {
  unsigned char key[32], pt[16], expect[16], out[16];
  hex_decode("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
  hex_decode("00112233445566778899aabbccddeeff", pt);
  hex_decode("8ea2b7ca516745bfeafc49904b496089", expect);
  ck_assert_int_eq(cwenc_aes_ecb_encrypt(256, key, pt, out), 0);
  ck_assert_mem_eq(out, expect, 16);
}
END_TEST

START_TEST(aes_ctr_is_its_own_inverse) {
  unsigned char key[32] = {1, 2, 3};
  unsigned char iv[16] = {0};
  unsigned char pt[16] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'};
  unsigned char ct[16], back[16];
  ck_assert_int_eq(cwenc_aes_ctr_xcrypt(256, key, iv, pt, ct, sizeof pt), 0);
  ck_assert_mem_ne(ct, pt, sizeof pt);
  ck_assert_int_eq(cwenc_aes_ctr_xcrypt(256, key, iv, ct, back, sizeof ct), 0);
  ck_assert_mem_eq(back, pt, sizeof pt);
}
END_TEST

START_TEST(aes_ctr_handles_cw_len_8) {
  unsigned char key[16] = {9, 9, 9};
  unsigned char iv[16] = {0};
  unsigned char pt[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
  unsigned char ct[8], back[8];
  ck_assert_int_eq(cwenc_aes_ctr_xcrypt(128, key, iv, pt, ct, sizeof pt), 0);
  ck_assert_int_eq(cwenc_aes_ctr_xcrypt(128, key, iv, ct, back, sizeof ct), 0);
  ck_assert_mem_eq(back, pt, sizeof pt);
}
END_TEST

START_TEST(hkdf_sha256_matches_vector) {
  unsigned char ikm[7], info[26], expect[32], out[32];
  hex_decode("4DA19FF0AF6B8F", ikm);
  memcpy(info, "annexd-cwenc-ctx-v01", 20);
  unsigned char tail[6] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x2A};
  memcpy(info + 20, tail, 6);
  hex_decode("0412be2156c0ed0e11fa0292ee6c2bb189d52ca1ed7b1c0c6bc8abec5bc0f3b0", expect);
  ck_assert_int_eq(cwenc_hkdf_sha256(ikm, sizeof ikm, info, sizeof info, out), 0);
  ck_assert_mem_eq(out, expect, 32);
}
END_TEST

START_TEST(config_off_when_algorithm_unset) {
  cwenc_config_t cfg;
  cwenc_ctx_t ctx;
  cwenc_selection_t sel;
  ck_assert_int_eq(cwenc_config_init(&cfg, NULL, NULL, NULL, NULL, NULL), 0);
  ck_assert_int_eq(cfg.algo, CWENC_ALGO_OFF);
  cwenc_ctx_init(&ctx, &cfg);
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), -1);
}
END_TEST

START_TEST(des56_rom_default_encrypts_known_vector) {
  cwenc_config_t cfg;
  cwenc_ctx_t ctx;
  cwenc_selection_t sel;
  unsigned char cw[8], expect[8];
  hex_decode("1122334455667788", cw);
  hex_decode("53c37859bba01f3e", expect);

  ck_assert_int_eq(cwenc_config_init(&cfg, "des56", NULL, NULL, NULL, NULL), 0);
  ck_assert_int_eq(cfg.fixed_key_len, 7);
  ck_assert_int_eq(cwenc_config_validate(&cfg, 8), 0);
  cwenc_ctx_init(&ctx, &cfg);
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
  ck_assert_int_eq(sel.fixed_key_mode, 0);
  ck_assert_int_eq(sel.algorithm_type, 0);

  ck_assert_int_eq(cwenc_encrypt_cw(&cfg, &sel, 8, 0, 0, 0, cw), 0);
  ck_assert_mem_eq(cw, expect, 8);
}
END_TEST

START_TEST(aes256_stream_fixed_key_matches_vector) {
  cwenc_config_t cfg;
  cwenc_ctx_t ctx;
  cwenc_selection_t sel;
  unsigned char cw[16], expect[16];
  hex_decode("00112233445566778899aabbccddeeff", cw);
  hex_decode("d3809272a5fbd5fc9ba7a6a0d96d49dc", expect);
  ck_assert_int_eq(cwenc_config_init(&cfg, "aes256", "stream", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", NULL, NULL), 0);
  ck_assert_int_eq(cwenc_config_validate(&cfg, 16), 0);
  cwenc_ctx_init(&ctx, &cfg);
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
  ck_assert_int_eq(sel.algorithm_type, 2);
  ck_assert_int_eq(cwenc_encrypt_cw(&cfg, &sel, 16, 1, 1, 0x2A, cw), 0);
  ck_assert_mem_eq(cw, expect, 16);
}
END_TEST

START_TEST(aes128_ecb_fixed_key_matches_vector) {
  cwenc_config_t cfg;
  cwenc_ctx_t ctx;
  cwenc_selection_t sel;
  unsigned char cw[16], expect[16];
  hex_decode("112233445566778899aabbccddeeff00", cw);
  hex_decode("ce57c949d9af6745ad0142a70a96b0a4", expect);

  ck_assert_int_eq(cwenc_config_init(&cfg, "aes128", "ecb", "000102030405060708090a0b0c0d0e0f", NULL, NULL), 0);
  ck_assert_int_eq(cwenc_config_validate(&cfg, 16), 0);
  cwenc_ctx_init(&ctx, &cfg);
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);

  ck_assert_int_eq(cwenc_encrypt_cw(&cfg, &sel, 16, 1, 1, 7, cw), 0);
  ck_assert_mem_eq(cw, expect, 16);
}
END_TEST

START_TEST(des56_key_list_mode_matches_vector) {
  cwenc_config_t cfg;
  cwenc_ctx_t ctx;
  cwenc_selection_t sel;
  unsigned char cw[8], expect[8];
  hex_decode("a1b2c3d4e5f60718", cw);
  hex_decode("6b0e1f28a3970d88", expect);

  memset(&cfg, 0, sizeof cfg);
  cfg.algo = CWENC_ALGO_DES56;
  cfg.key_list_a_loaded = 1;
  for (int i = 0; i < CWENC_KEY_LIST_LEN; i++) cfg.key_list_a[i] = (unsigned char)i;
  cwenc_ctx_init(&ctx, &cfg);
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
  ck_assert_int_eq(sel.cw_key_ptr, 14);
  ck_assert_int_eq(sel.fixed_key_mode, 1);
  ck_assert_int_eq(cwenc_encrypt_cw(&cfg, &sel, 8, 0, 0, 0, cw), 0);
  ck_assert_mem_eq(cw, expect, 8);
}
END_TEST

START_TEST(aes_ecb_rejected_on_cw_len_8_only) {
  cwenc_config_t cfg;
  ck_assert_int_eq(cwenc_config_init(&cfg, "aes128", "ecb", "000102030405060708090a0b0c0d0e0f", NULL, NULL), 0);
  ck_assert_int_eq(cwenc_config_validate(&cfg, 8), -1);
  ck_assert_int_eq(cwenc_config_validate(&cfg, 16), 0);
}
END_TEST

START_TEST(aes_with_no_key_and_no_list_fails_validate) {
  cwenc_config_t cfg;
  ck_assert_int_eq(cwenc_config_init(&cfg, "aes128", NULL, NULL, NULL, NULL), 0);
  ck_assert_int_eq(cfg.fixed_key_len, 0);
  ck_assert_int_eq(cwenc_config_validate(&cfg, 16), -1);
}
END_TEST

START_TEST(pack_param_bit_layout) {
  cwenc_selection_t sel = {1, 0, 2, 0x2A, {0}, 0};
  ck_assert_uint_eq(cwenc_pack_param(&sel), (unsigned short)(0x8000 | (2 << 11) | 0x2A));
}
END_TEST

START_TEST(pointer_rotation_single_list_stays_on_a) {
  cwenc_config_t cfg;
  cwenc_ctx_t ctx;
  cwenc_selection_t sel;
  memset(&cfg, 0, sizeof cfg);
  cfg.algo = CWENC_ALGO_DES56;
  cfg.key_list_a_loaded = 1;
  for (int i = 0; i < CWENC_KEY_LIST_LEN; i++)
    cfg.key_list_a[i] = (unsigned char)i;
  cwenc_ctx_init(&ctx, &cfg);

  for (int i = 0; i < 5; i++) {
    ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
    ck_assert_int_eq(sel.fixed_key_mode, 1);
    ck_assert_int_eq(sel.key_list_sel, 0);
    ck_assert_int_eq(sel.cw_key_ptr, i * CWENC_PTR_STEP);
    ck_assert_int_eq(sel.key_material_len, 7);
    ck_assert_uint_eq(sel.key_material[0], (unsigned char)(i * CWENC_PTR_STEP));
  }
}
END_TEST

START_TEST(pointer_rotation_wraps_and_alternates_lists) {
  cwenc_config_t cfg;
  cwenc_ctx_t ctx;
  cwenc_selection_t sel;
  int steps = (CWENC_PTR_MAX / CWENC_PTR_STEP) + 1;
  memset(&cfg, 0, sizeof cfg);
  cfg.algo = CWENC_ALGO_DES56;
  cfg.key_list_a_loaded = 1;
  cfg.key_list_b_loaded = 1;
  cwenc_ctx_init(&ctx, &cfg);
  for (int i = 0; i < steps; i++) {
    ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
    ck_assert_int_eq(sel.key_list_sel, 0);
  }
  ck_assert_int_eq(cwenc_select_next(&ctx, &sel), 0);
  ck_assert_int_eq(sel.key_list_sel, 1);
  ck_assert_int_eq(sel.cw_key_ptr, 0);
}
END_TEST

static Suite *cw_encryption_suite(void) {
  Suite *s = suite_create("cw_encryption");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, des56_expand_matches_annex_d_kat);
  tcase_add_test(tc, des_ecb_encrypt_matches_known_vector);
  tcase_add_test(tc, des_ecb_encrypt_rejects_non_block_length);
  tcase_add_test(tc, aes128_ecb_encrypt_matches_fips197);
  tcase_add_test(tc, aes256_ecb_encrypt_matches_fips197);
  tcase_add_test(tc, aes_ctr_is_its_own_inverse);
  tcase_add_test(tc, aes_ctr_handles_cw_len_8);
  tcase_add_test(tc, hkdf_sha256_matches_vector);
  tcase_add_test(tc, config_off_when_algorithm_unset);
  tcase_add_test(tc, des56_rom_default_encrypts_known_vector);
  tcase_add_test(tc, aes256_stream_fixed_key_matches_vector);
  tcase_add_test(tc, aes128_ecb_fixed_key_matches_vector);
  tcase_add_test(tc, des56_key_list_mode_matches_vector);
  tcase_add_test(tc, aes_ecb_rejected_on_cw_len_8_only);
  tcase_add_test(tc, aes_with_no_key_and_no_list_fails_validate);
  tcase_add_test(tc, pack_param_bit_layout);
  tcase_add_test(tc, pointer_rotation_single_list_stays_on_a);
  tcase_add_test(tc, pointer_rotation_wraps_and_alternates_lists);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cw_encryption_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
