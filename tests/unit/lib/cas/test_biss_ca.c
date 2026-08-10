/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/ca.h"

/* EBU Tech 3292-s1 Annex C worked example. cross-checked against openssl(1)
   pkeyutl/enc before transcribing */

static const char annex_c_priv_pem[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEowIBAAKCAQEAvkIDCEWUjaiPeNuROvTiGOAhtodNQAyBeIW66DiscEOddR1C\n"
    "ikj4NuW7KjW5Oic9/GFt0nCr2rYGVsHpWOdPvkSLybmtX5ndxni9CxomdcUQNNL9\n"
    "948cXBuNMThc6RPQlH1O7kL6B/ETBXrNJLTVQUABVnIVjr04PEMfi4cztGT964ul\n"
    "OQ5e/dQJUpxbG3aL5BxFSnX89JwSHgdn1dwtpOC9FGrEX3I41wxdR0OwKzI5klKv\n"
    "wgdsipofi/3JUI9rOuC+uhgET40uQxr27KL5Y+2NjEKoooqUZTD0Q+5CSWYXm6hq\n"
    "tHeBJB92/xD3Do0ZEDArQ5iViysH71q0ObjMowIDAQABAoIBAQCXUPMOciB7JfOt\n"
    "wJtaE4d7F09Y13VWGlwimeGUbfafdvdVPMc+KljXeJEKOh4uJSXEg0yQETJtSVXz\n"
    "TFglcBrZDbVL5BQCs+JRxpc7rDmocum3yZNZgAWjL/p0igpDCZJbdun+z2ACTva8\n"
    "5fUgW348XgZyVVvV4aJHM290TjyOHF5hOod4l0KjOlec5bAdz28CaEqftqytypQm\n"
    "IFXQ0+06k9Dthm8HCwl6NLWwDPe7s+vNMJlq+tR4Nfu7beEDT0ydgI30FB7rDMkZ\n"
    "I/Y2TL6HsiUBh83JIkj6g2SqkdzHjTI52fWDBZ1lCEzIKh+br5anaSun3YUFkEFU\n"
    "tXdWWClhAoGBAPtA3fk5rQuSq05hl4rMrl/FFRiEcuuMHF/0wojBHyac7plaQW6S\n"
    "eni9z5NMnfvNQVdjsUHKSYVRypMfodWmim4jtk3/FpwEjXniJFNPs2Yv+FFNBSPI\n"
    "ptIVIq6ext7NA7syeCy4INRF/Spj3hBIH9IsOx6IsQbY69/jwZcVzkTzAoGBAMHa\n"
    "JyxLTETxLuAQ5wuXbfWk4iq5so6zwDzbe97GlCTqL+YP6KsNKkbkc/1JDjYu8KKu\n"
    "bL8LE7bKMU+4dE97olZhvZSdi9O69P80iLlM/gK4a+6adjcc5DcacgIX1Xa41uk3\n"
    "oEGJaiqzdvgDjthYntJx1d3V1o3zBwniW4nkCQWRAoGAcJUSYbh8V7Ey3X5RXzpz\n"
    "8CnpWAERVYaEuGJ+QLT4dl7fcwvEQf2Ur0GuH3y3VbsVSkk7hhVUeE68DMyhwZBM\n"
    "eym5aJ2izeokUrcIO+R8qI9aH2P5p50jUwNxdPlkdzU6NMlam/8thrCNzk7NlFId\n"
    "IBn9q6LoX/8XQk1V05NLyA0CgYBizwyakkLs/TaUdWkfSm98/y9c8cxm2o6JNqLb\n"
    "+cI3UrtZfBBvZ8V93yKUHzHEQobblSbO9hl1WXhrFy0J+o3Tk/xrDSbhpHEOyDtM\n"
    "oEb1IgW52DebffmBcNRd5sIiwrKgq37fCOj5nQJuBnpAImPKBsYpTb8QGakjy6I3\n"
    "FenXUQKBgAvaPmlMVdpU5eAl23p7bn77FvvCgDklgMg11fl3hWZN6QGX+DMWS03+\n"
    "EkllfhoCuiSoA8lGCipLiAZZ5rfFBLUkQkrKTmKhTlgkXsB7akT6k1O1QVkn/2jb\n"
    "lsk0R8JgfaKjOvt3Ba57qvSAtLa8Misn5lmv9/kDrGILCYGRNw8a\n"
    "-----END RSA PRIVATE KEY-----\n";

static const char annex_c_pub_pem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvkIDCEWUjaiPeNuROvTi\n"
    "GOAhtodNQAyBeIW66DiscEOddR1Cikj4NuW7KjW5Oic9/GFt0nCr2rYGVsHpWOdP\n"
    "vkSLybmtX5ndxni9CxomdcUQNNL9948cXBuNMThc6RPQlH1O7kL6B/ETBXrNJLTV\n"
    "QUABVnIVjr04PEMfi4cztGT964ulOQ5e/dQJUpxbG3aL5BxFSnX89JwSHgdn1dwt\n"
    "pOC9FGrEX3I41wxdR0OwKzI5klKvwgdsipofi/3JUI9rOuC+uhgET40uQxr27KL5\n"
    "Y+2NjEKoooqUZTD0Q+5CSWYXm6hqtHeBJB92/xD3Do0ZEDArQ5iViysH71q0ObjM\n"
    "owIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

static const unsigned char annex_c_ekid[BISS_CA_EKID_LEN] = {0x1d, 0x68, 0xe8, 0xa4, 0x52, 0x15, 0x55, 0x23};

static const unsigned char annex_c_encrypted_session_data[BISS_CA_RSA_BYTES] = {
    0x4f, 0xdf, 0xa9, 0x8a, 0x0a, 0xb5, 0x5f, 0x68, 0x1f, 0x6f, 0xec, 0x2b, 0x38, 0xf4, 0x69, 0x7f,
    0x46, 0x0f, 0xc9, 0x20, 0x8e, 0xa2, 0xbb, 0xc5, 0x1b, 0x16, 0x84, 0xf5, 0x01, 0x18, 0x7f, 0x6e,
    0x09, 0xd5, 0x24, 0x30, 0x07, 0x54, 0x9f, 0x22, 0x43, 0x71, 0x87, 0xff, 0xa8, 0x2d, 0x2b, 0xb5,
    0x3e, 0xd5, 0xba, 0xed, 0x02, 0xd6, 0xbd, 0x1d, 0x0b, 0x58, 0x07, 0x41, 0x1a, 0xe0, 0x60, 0x23,
    0xcb, 0x92, 0x3b, 0x4f, 0x89, 0x5f, 0xdb, 0x5f, 0x61, 0x1f, 0x39, 0x70, 0x99, 0x40, 0x8a, 0xd1,
    0x75, 0x66, 0x21, 0xa0, 0x56, 0x90, 0x9b, 0xa8, 0x0a, 0x6f, 0xc5, 0x9e, 0x68, 0xa7, 0x1d, 0xe5,
    0x6a, 0xac, 0x60, 0xeb, 0xdc, 0x24, 0xe9, 0x3b, 0x1a, 0x0b, 0xc8, 0x7e, 0x16, 0x00, 0xfe, 0x75,
    0xea, 0xcf, 0xe0, 0x6a, 0x2d, 0x66, 0x73, 0x9f, 0x16, 0xcb, 0xc9, 0xe7, 0x4b, 0x7b, 0xcb, 0x08,
    0x88, 0xdf, 0x17, 0x77, 0xca, 0xc5, 0x8e, 0x0d, 0x14, 0x44, 0xe5, 0x5f, 0x4c, 0x80, 0x2b, 0x39,
    0xd8, 0xf4, 0x16, 0x37, 0x20, 0xe6, 0xdd, 0x50, 0x5c, 0x6d, 0xca, 0x7c, 0xa2, 0xd3, 0x95, 0x6d,
    0x45, 0x7e, 0x82, 0xe6, 0x8e, 0xc3, 0x98, 0x0a, 0x6e, 0xad, 0x3c, 0xfe, 0xa8, 0x88, 0xd3, 0xc2,
    0x5a, 0x4e, 0xc8, 0x8f, 0x60, 0x73, 0x7d, 0xe8, 0xf3, 0xa7, 0xb3, 0xd4, 0x07, 0xe2, 0x9a, 0x5c,
    0x39, 0x14, 0x06, 0x3f, 0xcb, 0x14, 0x04, 0xbf, 0x33, 0x37, 0x16, 0x2e, 0xdd, 0x04, 0xb7, 0x0c,
    0xd7, 0x30, 0x2a, 0x07, 0xcd, 0xd1, 0x0e, 0xe1, 0x84, 0x5d, 0xaf, 0xea, 0xa8, 0x4c, 0xc6, 0x92,
    0x43, 0x66, 0xad, 0xb2, 0x59, 0x3c, 0x58, 0x43, 0xcd, 0xe5, 0x1c, 0x37, 0x58, 0x52, 0x10, 0x1f,
    0x02, 0xe4, 0xd1, 0x65, 0xa6, 0x27, 0x28, 0xe6, 0x1b, 0x99, 0x21, 0x8f, 0x12, 0x3a, 0x8e, 0x24};

/* session_data plaintext (Table 9): session_key_descriptor + entitlement_flags_descriptor */
static const unsigned char annex_c_session_data[] = {
    0x00, 0x16, 0x81, 0x11, 0x00, 0x29, 0x82, 0x38, 0xbe, 0x84, 0xae, 0x1d, 0x6c, 0xd6, 0x2a, 0xe9,
    0x52, 0x90, 0x64, 0x9d, 0xf1, 0x82, 0x01, 0x00};

static const unsigned char annex_c_sk[BISS_CA_SK_LEN] = {0x29, 0x82, 0x38, 0xbe, 0x84, 0xae, 0x1d, 0x6c, 0xd6, 0x2a, 0xe9, 0x52, 0x90, 0x64, 0x9d, 0xf1};
static const unsigned char annex_c_iv[BISS_CA_IV_LEN] = {0x6d, 0xfd, 0xbf, 0x58, 0xb0, 0x39, 0x4b, 0x4a, 0xaa, 0xa4, 0xef, 0x86, 0x5f, 0x63, 0xbf, 0x86};
static const unsigned char annex_c_sw0[BISS_CA_SW_LEN] = {0x9d, 0x42, 0xc2, 0xec, 0xd2, 0xde, 0x5b, 0x15, 0xf2, 0x27, 0xae, 0xa7, 0xdb, 0xa5, 0xf8, 0xf0};
static const unsigned char annex_c_sw1[BISS_CA_SW_LEN] = {0x1c, 0x27, 0x3f, 0xc4, 0xbd, 0x66, 0x4b, 0x40, 0xfd, 0x8c, 0xd3, 0xb0, 0x5b, 0x26, 0xd3, 0x42};
static const unsigned char annex_c_esw0[BISS_CA_SW_LEN] = {0x21, 0x8b, 0xf6, 0xfa, 0xc3, 0x9f, 0xac, 0xe8, 0x25, 0xcd, 0x1e, 0xde, 0xb7, 0xbf, 0x6a, 0x17};
static const unsigned char annex_c_esw1[BISS_CA_SW_LEN] = {0x10, 0xf9, 0x3b, 0x6e, 0x5d, 0x94, 0xf5, 0xcc, 0x38, 0x52, 0x05, 0x74, 0xb1, 0x4b, 0x19, 0x40};

START_TEST(ekid_matches_annex_c_vector_from_public_key) {
  biss_ca_key_t *pub = biss_ca_key_load_public_mem(annex_c_pub_pem, sizeof annex_c_pub_pem - 1);
  unsigned char ekid[BISS_CA_EKID_LEN];
  ck_assert_ptr_nonnull(pub);
  ck_assert_int_eq(biss_ca_entitlement_key_id(pub, ekid), 0);
  ck_assert_mem_eq(ekid, annex_c_ekid, BISS_CA_EKID_LEN);
  biss_ca_key_free(pub);
}
END_TEST

START_TEST(ekid_matches_annex_c_vector_from_private_key) {
  biss_ca_key_t *priv = biss_ca_key_load_private_mem(annex_c_priv_pem, sizeof annex_c_priv_pem - 1);
  unsigned char ekid[BISS_CA_EKID_LEN];
  ck_assert_ptr_nonnull(priv);
  ck_assert_int_eq(biss_ca_entitlement_key_id(priv, ekid), 0);
  ck_assert_mem_eq(ekid, annex_c_ekid, BISS_CA_EKID_LEN);
  biss_ca_key_free(priv);
}
END_TEST

START_TEST(key_load_rejects_garbage_pem) {
  static const char garbage[] = "not a pem file at all";
  ck_assert_ptr_null(biss_ca_key_load_public_mem(garbage, sizeof garbage - 1));
  ck_assert_ptr_null(biss_ca_key_load_private_mem(garbage, sizeof garbage - 1));
}
END_TEST

START_TEST(rsa_decrypt_matches_annex_c_vector) {
  biss_ca_key_t *priv = biss_ca_key_load_private_mem(annex_c_priv_pem, sizeof annex_c_priv_pem - 1);
  unsigned char out[BISS_CA_SESSION_DATA_MAX];
  size_t out_len = 0;
  ck_assert_ptr_nonnull(priv);
  ck_assert_int_eq(biss_ca_rsa_decrypt(priv, annex_c_encrypted_session_data, out, sizeof out, &out_len), 0);
  ck_assert_uint_eq(out_len, sizeof annex_c_session_data);
  ck_assert_mem_eq(out, annex_c_session_data, sizeof annex_c_session_data);
  biss_ca_key_free(priv);
}
END_TEST

START_TEST(rsa_encrypt_decrypt_round_trips) {
  biss_ca_key_t *pub = biss_ca_key_load_public_mem(annex_c_pub_pem, sizeof annex_c_pub_pem - 1);
  biss_ca_key_t *priv = biss_ca_key_load_private_mem(annex_c_priv_pem, sizeof annex_c_priv_pem - 1);
  unsigned char cipher[BISS_CA_RSA_BYTES];
  unsigned char plain[BISS_CA_SESSION_DATA_MAX];
  size_t plain_len = 0;
  ck_assert_ptr_nonnull(pub);
  ck_assert_ptr_nonnull(priv);
  ck_assert_int_eq(biss_ca_rsa_encrypt(pub, annex_c_session_data, sizeof annex_c_session_data, cipher), 0);
  /* OAEP is randomized: ciphertext differs from the fixed vector, plaintext still matches */
  ck_assert_mem_ne(cipher, annex_c_encrypted_session_data, BISS_CA_RSA_BYTES);
  ck_assert_int_eq(biss_ca_rsa_decrypt(priv, cipher, plain, sizeof plain, &plain_len), 0);
  ck_assert_uint_eq(plain_len, sizeof annex_c_session_data);
  ck_assert_mem_eq(plain, annex_c_session_data, sizeof annex_c_session_data);
  biss_ca_key_free(pub);
  biss_ca_key_free(priv);
}
END_TEST

START_TEST(rsa_encrypt_rejects_oversized_input) {
  biss_ca_key_t *pub = biss_ca_key_load_public_mem(annex_c_pub_pem, sizeof annex_c_pub_pem - 1);
  unsigned char big[BISS_CA_SESSION_DATA_MAX + 1] = {0};
  unsigned char cipher[BISS_CA_RSA_BYTES];
  ck_assert_ptr_nonnull(pub);
  ck_assert_int_eq(biss_ca_rsa_encrypt(pub, big, sizeof big, cipher), -1);
  biss_ca_key_free(pub);
}
END_TEST

START_TEST(aes_cbc_encrypt_matches_annex_c_vector) {
  unsigned char esw0[BISS_CA_SW_LEN], esw1[BISS_CA_SW_LEN];
  ck_assert_int_eq(biss_ca_aes_cbc_encrypt(annex_c_sk, annex_c_iv, annex_c_sw0, esw0), 0);
  ck_assert_mem_eq(esw0, annex_c_esw0, BISS_CA_SW_LEN);
  ck_assert_int_eq(biss_ca_aes_cbc_encrypt(annex_c_sk, annex_c_iv, annex_c_sw1, esw1), 0);
  ck_assert_mem_eq(esw1, annex_c_esw1, BISS_CA_SW_LEN);
}
END_TEST

START_TEST(aes_cbc_decrypt_matches_annex_c_vector) {
  unsigned char sw0[BISS_CA_SW_LEN], sw1[BISS_CA_SW_LEN];
  ck_assert_int_eq(biss_ca_aes_cbc_decrypt(annex_c_sk, annex_c_iv, annex_c_esw0, sw0), 0);
  ck_assert_mem_eq(sw0, annex_c_sw0, BISS_CA_SW_LEN);
  ck_assert_int_eq(biss_ca_aes_cbc_decrypt(annex_c_sk, annex_c_iv, annex_c_esw1, sw1), 0);
  ck_assert_mem_eq(sw1, annex_c_sw1, BISS_CA_SW_LEN);
}
END_TEST

START_TEST(random_fills_requested_length) {
  unsigned char buf[32];
  memset(buf, 0xAA, sizeof buf);
  ck_assert_int_eq(biss_ca_random(buf, sizeof buf), 0);
  /* astronomically unlikely to still be all 0xAA after a real CSPRNG fill */
  ck_assert_mem_ne(buf, "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA", sizeof buf);
}
END_TEST

static Suite *biss_ca_suite(void) {
  Suite *s = suite_create("biss_ca");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ekid_matches_annex_c_vector_from_public_key);
  tcase_add_test(tc, ekid_matches_annex_c_vector_from_private_key);
  tcase_add_test(tc, key_load_rejects_garbage_pem);
  tcase_add_test(tc, rsa_decrypt_matches_annex_c_vector);
  tcase_add_test(tc, rsa_encrypt_decrypt_round_trips);
  tcase_add_test(tc, rsa_encrypt_rejects_oversized_input);
  tcase_add_test(tc, aes_cbc_encrypt_matches_annex_c_vector);
  tcase_add_test(tc, aes_cbc_decrypt_matches_annex_c_vector);
  tcase_add_test(tc, random_fills_requested_length);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(biss_ca_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
