/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/ca_sections.h"
#include "lib/demux/crc32.h"

/* EBU Tech 3292-s1 Annex C.3 worked example, session_data plaintext */
static const unsigned char annex_c_session_data[BISS_CA_SESSION_DATA_LEN] = {
    0x00, 0x16, 0x81, 0x11, 0x00, 0x29, 0x82, 0x38, 0xbe, 0x84, 0xae, 0x1d, 0x6c, 0xd6, 0x2a, 0xe9,
    0x52, 0x90, 0x64, 0x9d, 0xf1, 0x82, 0x01, 0x00};
static const unsigned char annex_c_sk[BISS_CA_SK_LEN] = {0x29, 0x82, 0x38, 0xbe, 0x84, 0xae, 0x1d, 0x6c, 0xd6, 0x2a, 0xe9, 0x52, 0x90, 0x64, 0x9d, 0xf1};

/* Annex C.4 */
static const unsigned char annex_c_iv[BISS_CA_IV_LEN] = {0x6d, 0xfd, 0xbf, 0x58, 0xb0, 0x39, 0x4b, 0x4a, 0xaa, 0xa4, 0xef, 0x86, 0x5f, 0x63, 0xbf, 0x86};
static const unsigned char annex_c_esw0[BISS_CA_SW_LEN] = {0x21, 0x8b, 0xf6, 0xfa, 0xc3, 0x9f, 0xac, 0xe8, 0x25, 0xcd, 0x1e, 0xde, 0xb7, 0xbf, 0x6a, 0x17};
static const unsigned char annex_c_esw1[BISS_CA_SW_LEN] = {0x10, 0xf9, 0x3b, 0x6e, 0x5d, 0x94, 0xf5, 0xcc, 0x38, 0x52, 0x05, 0x74, 0xb1, 0x4b, 0x19, 0x40};

START_TEST(session_data_matches_annex_c_vector) {
  unsigned char out[BISS_CA_SESSION_DATA_LEN];
  ck_assert_uint_eq(biss_ca_build_session_data(annex_c_sk, 0, out, sizeof out), BISS_CA_SESSION_DATA_LEN);
  ck_assert_mem_eq(out, annex_c_session_data, BISS_CA_SESSION_DATA_LEN);
}
END_TEST

START_TEST(session_data_parse_matches_annex_c_vector) {
  unsigned char sk[BISS_CA_SK_LEN];
  int parity = -1;
  ck_assert_int_eq(biss_ca_parse_session_data(annex_c_session_data, sizeof annex_c_session_data, sk, &parity), 0);
  ck_assert_int_eq(parity, 0);
  ck_assert_mem_eq(sk, annex_c_sk, BISS_CA_SK_LEN);
}
END_TEST

START_TEST(session_data_round_trips_odd_parity) {
  unsigned char sw[BISS_CA_SK_LEN] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  unsigned char out[BISS_CA_SESSION_DATA_LEN], sk[BISS_CA_SK_LEN];
  int parity = -1;
  ck_assert_uint_eq(biss_ca_build_session_data(sw, 1, out, sizeof out), BISS_CA_SESSION_DATA_LEN);
  ck_assert_int_eq(biss_ca_parse_session_data(out, sizeof out, sk, &parity), 0);
  ck_assert_int_eq(parity, 1);
  ck_assert_mem_eq(sk, sw, BISS_CA_SK_LEN);
}
END_TEST

START_TEST(entitlement_session_id_desc_round_trips) {
  unsigned char buf[8];
  unsigned esid = 0, onid = 0;
  ck_assert_uint_eq(biss_ca_build_entitlement_session_id_desc(0x1234, 0x5678, buf, sizeof buf), 6);
  ck_assert_int_eq(biss_ca_parse_entitlement_session_id_desc(buf, 6, &esid, &onid), 0);
  ck_assert_uint_eq(esid, 0x1234);
  ck_assert_uint_eq(onid, 0x5678);
}
END_TEST

START_TEST(entitlement_session_id_desc_rejects_wrong_tag) {
  unsigned char buf[6] = {0x7F, 4, 0, 0, 0, 0};
  unsigned esid, onid;
  ck_assert_int_eq(biss_ca_parse_entitlement_session_id_desc(buf, sizeof buf, &esid, &onid), -1);
}
END_TEST

START_TEST(emm_section_round_trips_single_entry) {
  unsigned char sec[4096];
  biss_ca_emm_entry_t entry;
  size_t len;
  biss_ca_emm_parsed_t parsed;
  const unsigned char *sd;

  memset(entry.entitlement_key_id, 0xAB, BISS_CA_EKID_LEN);
  memset(entry.encrypted_session_data, 0xCD, BISS_CA_RSA_BYTES);

  len = biss_ca_build_emm_section(0x81, 0x1234, 3, 0, 0, 0x5678, 0x81, &entry, 1, sec, sizeof sec);
  ck_assert_uint_gt(len, 0);
  ck_assert_uint_eq(crc32_mpeg(sec, len), 0);

  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &parsed), 0);
  ck_assert_uint_eq(parsed.table_id, 0x81);
  ck_assert_uint_eq(parsed.esid, 0x1234);
  ck_assert_uint_eq(parsed.onid, 0x5678);
  ck_assert_uint_eq(parsed.n_entries, 1);

  sd = biss_ca_emm_find_entry(&parsed, entry.entitlement_key_id);
  ck_assert_ptr_nonnull(sd);
  ck_assert_mem_eq(sd, entry.encrypted_session_data, BISS_CA_RSA_BYTES);
}
END_TEST

START_TEST(emm_section_round_trips_multiple_entries) {
  unsigned char sec[4096];
  biss_ca_emm_entry_t entries[3];
  size_t len, i;
  biss_ca_emm_parsed_t parsed;

  for (i = 0; i < 3; i++) {
    memset(entries[i].entitlement_key_id, (int)i + 1, BISS_CA_EKID_LEN);
    memset(entries[i].encrypted_session_data, (int)i + 0x10, BISS_CA_RSA_BYTES);
  }

  len = biss_ca_build_emm_section(0x85, 0xBEEF, 0, 0, 0, 0x1, 0x85, entries, 3, sec, sizeof sec);
  ck_assert_uint_gt(len, 0);
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &parsed), 0);
  ck_assert_uint_eq(parsed.n_entries, 3);

  for (i = 0; i < 3; i++) {
    const unsigned char *sd = biss_ca_emm_find_entry(&parsed, entries[i].entitlement_key_id);
    ck_assert_ptr_nonnull(sd);
    ck_assert_mem_eq(sd, entries[i].encrypted_session_data, BISS_CA_RSA_BYTES);
  }
}
END_TEST

START_TEST(emm_find_entry_returns_null_for_unknown_ekid) {
  unsigned char sec[4096];
  biss_ca_emm_entry_t entry;
  unsigned char unknown[BISS_CA_EKID_LEN];
  size_t len;
  biss_ca_emm_parsed_t parsed;

  memset(entry.entitlement_key_id, 0x11, BISS_CA_EKID_LEN);
  memset(entry.encrypted_session_data, 0x22, BISS_CA_RSA_BYTES);
  memset(unknown, 0x99, BISS_CA_EKID_LEN);

  len = biss_ca_build_emm_section(0x81, 1, 0, 0, 0, 1, 0x81, &entry, 1, sec, sizeof sec);
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &parsed), 0);
  ck_assert_ptr_null(biss_ca_emm_find_entry(&parsed, unknown));
}
END_TEST

START_TEST(emm_section_rejects_corrupted_crc) {
  unsigned char sec[4096];
  biss_ca_emm_entry_t entry;
  size_t len;
  biss_ca_emm_parsed_t parsed;

  memset(entry.entitlement_key_id, 0x11, BISS_CA_EKID_LEN);
  memset(entry.encrypted_session_data, 0x22, BISS_CA_RSA_BYTES);
  len = biss_ca_build_emm_section(0x81, 1, 0, 0, 0, 1, 0x81, &entry, 1, sec, sizeof sec);
  sec[len - 1] ^= 0xFF;
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &parsed), -1);
}
END_TEST

START_TEST(emm_section_rejects_bad_table_id) {
  unsigned char sec[4096];
  biss_ca_emm_entry_t entry;
  size_t len;
  biss_ca_emm_parsed_t parsed;

  memset(entry.entitlement_key_id, 0x11, BISS_CA_EKID_LEN);
  memset(entry.encrypted_session_data, 0x22, BISS_CA_RSA_BYTES);
  len = biss_ca_build_emm_section(0x81, 1, 0, 0, 0, 1, 0x81, &entry, 1, sec, sizeof sec);
  sec[0] = 0x80; /* ECM table_id, not a valid EMM range */
  /* corrupting table_id after CRC was computed also breaks the CRC, both checks must reject */
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &parsed), -1);
}
END_TEST

START_TEST(emm_section_build_rejects_overflow) {
  unsigned char sec[16]; /* far too small for even the header */
  biss_ca_emm_entry_t entry;
  memset(&entry, 0, sizeof entry);
  ck_assert_uint_eq(biss_ca_build_emm_section(0x81, 1, 0, 0, 0, 1, 0x81, &entry, 1, sec, sizeof sec), 0);
}
END_TEST

START_TEST(ecm_section_round_trips_annex_c_vector) {
  unsigned char sec[128];
  size_t len;
  biss_ca_ecm_parsed_t parsed;

  len = biss_ca_build_ecm_section(0x1234, 0, 0, 0, 0x5678, 0, annex_c_iv, annex_c_esw0, annex_c_esw1, sec, sizeof sec);
  ck_assert_uint_gt(len, 0);
  ck_assert_uint_eq(crc32_mpeg(sec, len), 0);

  ck_assert_int_eq(biss_ca_parse_ecm_section(sec, len, &parsed), 0);
  ck_assert_uint_eq(parsed.esid, 0x1234);
  ck_assert_uint_eq(parsed.onid, 0x5678);
  ck_assert_int_eq(parsed.session_key_parity, 0);
  ck_assert_mem_eq(parsed.iv, annex_c_iv, BISS_CA_IV_LEN);
  ck_assert_mem_eq(parsed.esw_even, annex_c_esw0, BISS_CA_SW_LEN);
  ck_assert_mem_eq(parsed.esw_odd, annex_c_esw1, BISS_CA_SW_LEN);
}
END_TEST

START_TEST(ecm_section_round_trips_odd_parity) {
  unsigned char sec[128];
  size_t len;
  biss_ca_ecm_parsed_t parsed;

  len = biss_ca_build_ecm_section(1, 0, 0, 0, 1, 1, annex_c_iv, annex_c_esw0, annex_c_esw1, sec, sizeof sec);
  ck_assert_int_eq(biss_ca_parse_ecm_section(sec, len, &parsed), 0);
  ck_assert_int_eq(parsed.session_key_parity, 1);
}
END_TEST

START_TEST(ecm_section_rejects_truncated_section) {
  unsigned char sec[128];
  size_t len;
  biss_ca_ecm_parsed_t parsed;

  len = biss_ca_build_ecm_section(1, 0, 0, 0, 1, 0, annex_c_iv, annex_c_esw0, annex_c_esw1, sec, sizeof sec);
  ck_assert_int_eq(biss_ca_parse_ecm_section(sec, len - 1, &parsed), -1);
}
END_TEST

static Suite *biss_ca_sections_suite(void) {
  Suite *s = suite_create("biss_ca_sections");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, session_data_matches_annex_c_vector);
  tcase_add_test(tc, session_data_parse_matches_annex_c_vector);
  tcase_add_test(tc, session_data_round_trips_odd_parity);
  tcase_add_test(tc, entitlement_session_id_desc_round_trips);
  tcase_add_test(tc, entitlement_session_id_desc_rejects_wrong_tag);
  tcase_add_test(tc, emm_section_round_trips_single_entry);
  tcase_add_test(tc, emm_section_round_trips_multiple_entries);
  tcase_add_test(tc, emm_find_entry_returns_null_for_unknown_ekid);
  tcase_add_test(tc, emm_section_rejects_corrupted_crc);
  tcase_add_test(tc, emm_section_rejects_bad_table_id);
  tcase_add_test(tc, emm_section_build_rejects_overflow);
  tcase_add_test(tc, ecm_section_round_trips_annex_c_vector);
  tcase_add_test(tc, ecm_section_round_trips_odd_parity);
  tcase_add_test(tc, ecm_section_rejects_truncated_section);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(biss_ca_sections_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
