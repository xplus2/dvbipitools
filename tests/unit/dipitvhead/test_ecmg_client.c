/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/cas/ecmg_client.h"
#include "dipitvhead/cas/simulcrypt_msg.h"

static int find_tlv(const unsigned char *payload, size_t payload_len, unsigned short want_tag, const unsigned char **val_out, unsigned short *len_out) {
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;
  simulcrypt_tlv_reader_init(&r, payload, payload_len);
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == want_tag) {
      *val_out = val;
      *len_out = vlen;
      return 1;
    }
  }
  return 0;
}

START_TEST(channel_setup_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = ecmg_build_channel_setup(buf, sizeof buf, 3, 0x4A750002);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 3);
  ck_assert_uint_eq(hdr.type, ECMG_MSG_CHANNEL_SETUP);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_ECM_CHANNEL_ID, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 2);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], ECMG_CHANNEL_ID);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_SUPER_CAS_ID, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 4);
  ck_assert_uint_eq(((unsigned)val[0] << 24) | ((unsigned)val[1] << 16) | ((unsigned)val[2] << 8) | val[3], 0x4A750002u);
}
END_TEST

START_TEST(channel_setup_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(ecmg_build_channel_setup(buf, sizeof buf, 3, 1), 0u);
}
END_TEST

START_TEST(stream_setup_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = ecmg_build_stream_setup(buf, sizeof buf, 3, 0x1234, 100);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, ECMG_MSG_STREAM_SETUP);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_ECM_STREAM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], ECMG_STREAM_ID);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_ECM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 0x1234u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_NOMINAL_CP_DURATION, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 100u);
}
END_TEST

START_TEST(stream_setup_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(ecmg_build_stream_setup(buf, sizeof buf, 3, 1, 1), 0u);
}
END_TEST

START_TEST(cw_provision_lead0_permsg1_sends_one_combo_at_cp) {
  unsigned char buf[128];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_hdr_t hdr;
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;
  int combos = 0;

  memset(hist, 0, sizeof hist);
  size_t n = ecmg_build_cw_provision(buf, sizeof buf, 3, 500, hist, 8, 0, 1);
  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, ECMG_MSG_CW_PROVISION);

  simulcrypt_tlv_reader_init(&r, buf + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION) {
      ck_assert_uint_eq(vlen, 2 + 8);
      ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 500);
      combos++;
    }
  }
  ck_assert_int_eq(combos, 1);
}
END_TEST

START_TEST(cw_provision_lead1_permsg2_sends_current_and_next) {
  unsigned char buf[128];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_hdr_t hdr;
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;
  unsigned short seen_cp[4];
  int combos = 0;

  memset(hist, 0, sizeof hist);
  size_t n = ecmg_build_cw_provision(buf, sizeof buf, 3, 500, hist, 16, 1, 2);
  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);

  simulcrypt_tlv_reader_init(&r, buf + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION) {
      ck_assert_uint_eq(vlen, 2 + 16);
      ck_assert_int_lt(combos, 4);
      seen_cp[combos] = (unsigned short)(((unsigned)val[0] << 8) | val[1]);
      combos++;
    }
  }
  ck_assert_int_eq(combos, 2);
  ck_assert_uint_eq(seen_cp[0], 500);
  ck_assert_uint_eq(seen_cp[1], 501);
}
END_TEST

START_TEST(cw_provision_reuses_history_for_overlapping_cp) {
  unsigned char buf1[128], buf2[128];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_hdr_t hdr;
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;
  unsigned char cw_cp501_first[16];
  int found;

  memset(hist, 0, sizeof hist);
  /* CP=500: combo for [500,501] - caches CW(501) */
  size_t n1 = ecmg_build_cw_provision(buf1, sizeof buf1, 3, 500, hist, 16, 1, 2);
  ck_assert_uint_gt(n1, 0u);
  simulcrypt_hdr_parse(buf1, n1, &hdr);
  simulcrypt_tlv_reader_init(&r, buf1 + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  found = 0;
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION && (((unsigned)val[0] << 8) | val[1]) == 501) {
      memcpy(cw_cp501_first, val + 2, 16);
      found = 1;
    }
  }
  ck_assert_int_eq(found, 1);

  /* CP=501: combo for [501,502] - CW(501) must be the SAME value cached above, not fresh randomness */
  size_t n2 = ecmg_build_cw_provision(buf2, sizeof buf2, 3, 501, hist, 16, 1, 2);
  ck_assert_uint_gt(n2, 0u);
  simulcrypt_hdr_parse(buf2, n2, &hdr);
  simulcrypt_tlv_reader_init(&r, buf2 + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  found = 0;
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION && (((unsigned)val[0] << 8) | val[1]) == 501) {
      ck_assert_mem_eq(val + 2, cw_cp501_first, 16);
      found = 1;
    }
  }
  ck_assert_int_eq(found, 1);
}
END_TEST

START_TEST(cw_provision_rejects_small_cap) {
  unsigned char buf[10];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  memset(hist, 0, sizeof hist);
  ck_assert_uint_eq(ecmg_build_cw_provision(buf, sizeof buf, 3, 1, hist, 16, 0, 1), 0u);
}
END_TEST

START_TEST(find_error_status_locates_tag) {
  unsigned char buf[32];
  simulcrypt_writer_t w;
  unsigned short err;
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_ERROR);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ERROR_STATUS, (unsigned char[]){0x00, 0x02}, 2);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_find_error_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &err), 0);
  ck_assert_uint_eq(err, ECMG_ERR_UNSUPPORTED_PROTOCOL_VERSION);
}
END_TEST

START_TEST(find_error_status_absent_returns_error) {
  unsigned char buf[32];
  simulcrypt_writer_t w;
  unsigned short err;
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_ERROR);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_find_error_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &err), -1);
}
END_TEST

START_TEST(parse_channel_status_full_message) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_STATUS);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_LEAD_CW, (unsigned char[]){1}, 1);
  simulcrypt_writer_put_tlv(&w, ECMG_P_CW_PER_MSG, (unsigned char[]){2}, 1);
  simulcrypt_writer_put_tlv(&w, ECMG_P_MAX_COMP_TIME, (unsigned char[]){0x00, 0x64}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_MIN_CP_DURATION, (unsigned char[]){0x00, 0x0A}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_REP_PERIOD, (unsigned char[]){0x00, 0x64}, 2);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_parse_channel_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms), 0);
  ck_assert_uint_eq(lead_cw, 1);
  ck_assert_uint_eq(cw_per_msg, 2);
  ck_assert_uint_eq(max_comp_time_ms, 100);
  ck_assert_uint_eq(min_cp_100ms, 10);
  ck_assert_uint_eq(ecm_rep_period_ms, 100);
}
END_TEST

START_TEST(parse_channel_status_missing_cw_per_msg_fails) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_STATUS);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_LEAD_CW, (unsigned char[]){1}, 1);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_parse_channel_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms), -1);
}
END_TEST

START_TEST(parse_channel_status_rejects_out_of_range_cw_per_msg) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_STATUS);
  simulcrypt_writer_put_tlv(&w, ECMG_P_CW_PER_MSG, (unsigned char[]){(unsigned char)(ECMG_MAX_CW_PER_MSG + 1)}, 1);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_parse_channel_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms), -1);
}
END_TEST

START_TEST(cw_valid_frozen_always_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_FROZEN, 0, 10, 1000, 0), 1);
}
END_TEST

START_TEST(cw_valid_cycling_always_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 1000, 0), 1);
}
END_TEST

START_TEST(cw_valid_unscrambled_connected_is_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 1, 10, 1000, 0), 1);
}
END_TEST

START_TEST(cw_valid_unscrambled_within_one_cp_is_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 0, 10, 109, 100), 1);
}
END_TEST

START_TEST(cw_valid_unscrambled_past_one_cp_is_invalid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 0, 10, 110, 100), 0);
}
END_TEST

START_TEST(cw_valid_unscrambled_no_cadence_is_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 0, 0, 100000, 0), 1);
}
END_TEST

START_TEST(target_parity_frozen_ignores_elapsed_time) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_FROZEN, 0, 10, 1000, 0, 5), 1);
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_FROZEN, 0, 10, 1000, 0, 4), 0);
}
END_TEST

START_TEST(target_parity_cycling_connected_uses_epoch_directly) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 1, 10, 100000, 0, 3), 1);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_flips_after_one_cp) {
  /* epoch=3 (odd/1), one whole CP elapsed since publish -> flips to even/0 */
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 110, 100, 3), 0);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_flips_back_after_two_cp) {
  /* two whole CPs elapsed -> back to the original parity */
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 120, 100, 3), 1);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_within_cp_unchanged) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 105, 100, 3), 1);
}
END_TEST

static Suite *ecmg_client_suite(void) {
  Suite *s = suite_create("ecmg_client");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, channel_setup_builds_expected_fields);
  tcase_add_test(tc, channel_setup_rejects_small_cap);
  tcase_add_test(tc, stream_setup_builds_expected_fields);
  tcase_add_test(tc, stream_setup_rejects_small_cap);
  tcase_add_test(tc, cw_provision_lead0_permsg1_sends_one_combo_at_cp);
  tcase_add_test(tc, cw_provision_lead1_permsg2_sends_current_and_next);
  tcase_add_test(tc, cw_provision_reuses_history_for_overlapping_cp);
  tcase_add_test(tc, cw_provision_rejects_small_cap);
  tcase_add_test(tc, find_error_status_locates_tag);
  tcase_add_test(tc, find_error_status_absent_returns_error);
  tcase_add_test(tc, parse_channel_status_full_message);
  tcase_add_test(tc, parse_channel_status_missing_cw_per_msg_fails);
  tcase_add_test(tc, parse_channel_status_rejects_out_of_range_cw_per_msg);
  tcase_add_test(tc, cw_valid_frozen_always_valid);
  tcase_add_test(tc, cw_valid_cycling_always_valid);
  tcase_add_test(tc, cw_valid_unscrambled_connected_is_valid);
  tcase_add_test(tc, cw_valid_unscrambled_within_one_cp_is_valid);
  tcase_add_test(tc, cw_valid_unscrambled_past_one_cp_is_invalid);
  tcase_add_test(tc, cw_valid_unscrambled_no_cadence_is_valid);
  tcase_add_test(tc, target_parity_frozen_ignores_elapsed_time);
  tcase_add_test(tc, target_parity_cycling_connected_uses_epoch_directly);
  tcase_add_test(tc, target_parity_cycling_disconnected_flips_after_one_cp);
  tcase_add_test(tc, target_parity_cycling_disconnected_flips_back_after_two_cp);
  tcase_add_test(tc, target_parity_cycling_disconnected_within_cp_unchanged);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ecmg_client_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
