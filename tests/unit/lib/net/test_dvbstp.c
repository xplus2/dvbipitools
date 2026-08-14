/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/net/dvbstp.h"

/* fills buf[0..hdrlen) per clause 5.4.1.3, returns hdrlen */
static size_t build_header(unsigned char *buf, unsigned payload_id, unsigned segment_id, unsigned version,
                            unsigned section_number, unsigned last_section_number,
                            int crc_present, int has_provider_id, unsigned provider_id, unsigned priv_words) {
  size_t hdrlen;
  buf[0] = (unsigned char)(crc_present ? 0x01 : 0x00);
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 0;
  buf[4] = (unsigned char)(payload_id & 0xFF);
  buf[5] = (unsigned char)((segment_id >> 8) & 0xFF);
  buf[6] = (unsigned char)(segment_id & 0xFF);
  buf[7] = (unsigned char)(version & 0xFF);
  buf[8] = (unsigned char)((section_number >> 4) & 0xFF);
  buf[9] = (unsigned char)(((section_number & 0x0F) << 4) | ((last_section_number >> 8) & 0x0F));
  buf[10] = (unsigned char)(last_section_number & 0xFF);
  buf[11] = (unsigned char)((has_provider_id ? 0x10 : 0x00) | (priv_words & 0x0F));
  hdrlen = 12;
  if (has_provider_id) {
    buf[12] = (unsigned char)((provider_id >> 24) & 0xFF);
    buf[13] = (unsigned char)((provider_id >> 16) & 0xFF);
    buf[14] = (unsigned char)((provider_id >> 8) & 0xFF);
    buf[15] = (unsigned char)(provider_id & 0xFF);
    hdrlen += 4;
  }
  hdrlen += 4 * (size_t)priv_words;
  return hdrlen;
}

START_TEST(parse_header_reads_minimal_fields) {
  unsigned char buf[12];
  dvbstp_header_t h;
  size_t hdrlen = build_header(buf, 0xA3, 0x1234, 7, 2, 5, 0, 0, 0, 0);
  ck_assert_uint_eq(hdrlen, 12u);
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 12u);
  ck_assert_uint_eq(h.payload_id, 0xA3u);
  ck_assert_uint_eq(h.segment_id, 0x1234u);
  ck_assert_uint_eq(h.segment_version, 7u);
  ck_assert_uint_eq(h.section_number, 2u);
  ck_assert_uint_eq(h.last_section_number, 5u);
  ck_assert_int_eq(h.crc_present, 0);
  ck_assert_int_eq(h.has_provider_id, 0);
}
END_TEST

START_TEST(parse_header_reads_provider_id) {
  unsigned char buf[16];
  dvbstp_header_t h;
  size_t hdrlen = build_header(buf, 1, 1, 1, 0, 0, 0, 1, 0xDEADBEEFu, 0);
  ck_assert_uint_eq(hdrlen, 16u);
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 16u);
  ck_assert_int_eq(h.has_provider_id, 1);
  ck_assert_uint_eq(h.provider_id, 0xDEADBEEFu);
}
END_TEST

START_TEST(parse_header_skips_private_words) {
  unsigned char buf[12 + 8];
  dvbstp_header_t h;
  size_t hdrlen = build_header(buf, 1, 1, 1, 0, 0, 0, 0, 0, 2);
  ck_assert_uint_eq(hdrlen, 20u);
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 20u);
}
END_TEST

START_TEST(parse_header_rejects_short_buffer) {
  unsigned char buf[11];
  dvbstp_header_t h;
  memset(buf, 0, sizeof buf);
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
}
END_TEST

START_TEST(parse_header_rejects_nonzero_version) {
  unsigned char buf[12];
  dvbstp_header_t h;
  build_header(buf, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  buf[0] |= 0x40; /* version bits = 1 */
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
}
END_TEST

START_TEST(parse_header_rejects_nonzero_compression) {
  unsigned char buf[12];
  dvbstp_header_t h;
  build_header(buf, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  buf[11] |= 0x20; /* compr = 1 */
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
}
END_TEST

START_TEST(parse_header_rejects_nonzero_compression_broadcast_discovery) {
  unsigned char buf[12];
  dvbstp_header_t h;
  build_header(buf, 2, 1, 1, 0, 0, 0, 0, 0, 0);
  buf[11] |= 0x20; /* compr = 1 */
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
}
END_TEST

START_TEST(parse_header_accepts_compression_for_bcg_payload) {
  unsigned char buf[12];
  dvbstp_header_t h;
  build_header(buf, 0xA3, 1, 1, 0, 0, 0, 0, 0, 0);
  buf[11] |= 0x20; /* compr = 1 (BiM/binary, TS 102 539 table 3) */
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 12u);
  ck_assert_uint_eq(h.compr, 1u);
}
END_TEST

START_TEST(parse_header_rejects_truncated_provider_id) {
  unsigned char buf[15];
  dvbstp_header_t h;
  unsigned char full[16];
  build_header(full, 1, 1, 1, 0, 0, 0, 1, 0x11223344u, 0);
  memcpy(buf, full, sizeof buf);
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
}
END_TEST

START_TEST(parse_header_rejects_truncated_private_words) {
  unsigned char buf[12 + 8];
  dvbstp_header_t h;
  unsigned char full[12 + 8];
  build_header(full, 1, 1, 1, 0, 0, 0, 0, 0, 2);
  memcpy(buf, full, 12 + 4); /* only one of two private words present */
  ck_assert_uint_eq(dvbstp_parse_header(buf, 12 + 4, &h), 0u);
}
END_TEST

START_TEST(reasm_feed_single_section_completes_immediately) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt[12 + 5];
  dvbstp_header_t out_h;
  const unsigned char *out_data;
  size_t out_len;
  size_t hdrlen = build_header(pkt, 0xA3, 1, 1, 0, 0, 0, 0, 0, 0);
  memcpy(pkt + hdrlen, "hello", 5);

  ck_assert_ptr_nonnull(r);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, &out_h, &out_data, &out_len), 1);
  ck_assert_uint_eq(out_len, 5u);
  ck_assert_mem_eq(out_data, "hello", 5);
  ck_assert_uint_eq(out_h.payload_id, 0xA3u);
  ck_assert_uint_eq(out_h.segment_id, 1u);

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_reassembles_two_sections_in_order) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt0[12 + 3], pkt1[12 + 4];
  const unsigned char *out_data;
  size_t out_len;
  size_t h0 = build_header(pkt0, 1, 1, 1, 0, 1, 0, 0, 0, 0);
  size_t h1 = build_header(pkt1, 1, 1, 1, 1, 1, 0, 0, 0, 0);
  memcpy(pkt0 + h0, "abc", 3);
  memcpy(pkt1 + h1, "defg", 4);

  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt0, sizeof pkt0, NULL, &out_data, &out_len), 0);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt1, sizeof pkt1, NULL, &out_data, &out_len), 1);
  ck_assert_uint_eq(out_len, 7u);
  ck_assert_mem_eq(out_data, "abcdefg", 7);

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_verifies_crc_on_final_section) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt[12 + 5 + 4];
  const unsigned char *out_data;
  size_t out_len;
  uint32_t crc;
  size_t hdrlen = build_header(pkt, 1, 1, 1, 0, 0, 1, 0, 0, 0);
  memcpy(pkt + hdrlen, "hello", 5);
  crc = crc32_mpeg((const unsigned char *)"hello", 5);
  pkt[hdrlen + 5] = (unsigned char)((crc >> 24) & 0xFF);
  pkt[hdrlen + 6] = (unsigned char)((crc >> 16) & 0xFF);
  pkt[hdrlen + 7] = (unsigned char)((crc >> 8) & 0xFF);
  pkt[hdrlen + 8] = (unsigned char)(crc & 0xFF);

  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 1);
  ck_assert_uint_eq(out_len, 5u);
  ck_assert_mem_eq(out_data, "hello", 5);

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_crc_mismatch) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt[12 + 5 + 4];
  const unsigned char *out_data;
  size_t out_len;
  size_t hdrlen = build_header(pkt, 1, 1, 1, 0, 0, 1, 0, 0, 0);
  memcpy(pkt + hdrlen, "hello", 5);
  memset(pkt + hdrlen + 5, 0, 4); /* wrong crc */

  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_short_packet) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char buf[11];
  const unsigned char *out_data;
  size_t out_len;
  memset(buf, 0, sizeof buf);
  ck_assert_int_eq(dvbstp_reasm_feed(r, buf, sizeof buf, NULL, &out_data, &out_len), 0);
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_section_number_past_last) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt[12 + 1];
  const unsigned char *out_data;
  size_t out_len;
  size_t hdrlen = build_header(pkt, 1, 1, 1, 2, 1, 0, 0, 0, 0); /* section 2 > last 1 */
  pkt[hdrlen] = 'x';
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_last_section_number_too_large) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt[12 + 1];
  const unsigned char *out_data;
  size_t out_len;
  size_t hdrlen = build_header(pkt, 1, 1, 1, 0, 64, 0, 0, 0, 0); /* REASM_MAX_SECTIONS == 64 */
  pkt[hdrlen] = 'x';
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_crc_flag_on_non_final_section) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt[12 + 1];
  const unsigned char *out_data;
  size_t out_len;
  size_t hdrlen = build_header(pkt, 1, 1, 1, 0, 1, 1, 0, 0, 0); /* crc on section 0 of 0..1 */
  pkt[hdrlen] = 'x';
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_ignores_duplicate_section) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt0[12 + 3], pkt1[12 + 4];
  const unsigned char *out_data;
  size_t out_len;
  size_t h0 = build_header(pkt0, 1, 1, 1, 0, 1, 0, 0, 0, 0);
  size_t h1 = build_header(pkt1, 1, 1, 1, 1, 1, 0, 0, 0, 0);
  memcpy(pkt0 + h0, "abc", 3);
  memcpy(pkt1 + h1, "defg", 4);

  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt0, sizeof pkt0, NULL, &out_data, &out_len), 0);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt0, sizeof pkt0, NULL, &out_data, &out_len), 0); /* repeat, no crash/corruption */
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt1, sizeof pkt1, NULL, &out_data, &out_len), 1);
  ck_assert_uint_eq(out_len, 7u);
  ck_assert_mem_eq(out_data, "abcdefg", 7);

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_version_bump_resets_in_progress_slot) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt_v1[12 + 3], pkt_v2_0[12 + 3], pkt_v2_1[12 + 4];
  const unsigned char *out_data;
  size_t out_len;
  size_t h;

  h = build_header(pkt_v1, 1, 1, 1, 0, 1, 0, 0, 0, 0);
  memcpy(pkt_v1 + h, "old", 3);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt_v1, sizeof pkt_v1, NULL, &out_data, &out_len), 0);

  h = build_header(pkt_v2_0, 1, 1, 2, 0, 1, 0, 0, 0, 0); /* version bumped to 2 */
  memcpy(pkt_v2_0 + h, "new", 3);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt_v2_0, sizeof pkt_v2_0, NULL, &out_data, &out_len), 0);

  h = build_header(pkt_v2_1, 1, 1, 2, 1, 1, 0, 0, 0, 0);
  memcpy(pkt_v2_1 + h, "data", 4);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt_v2_1, sizeof pkt_v2_1, NULL, &out_data, &out_len), 1);
  ck_assert_uint_eq(out_len, 7u);
  ck_assert_mem_eq(out_data, "newdata", 7); /* v1's section carried over: v2's data only */

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_evicts_oldest_slot_once_all_slots_are_busy) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  const unsigned char *out_data;
  size_t out_len;
  int i;

  /* fill all 8 slots with incomplete two-section segments (segment_id 0..7) */
  for (i = 0; i < 8; i++) {
    unsigned char pkt[12 + 1];
    size_t h = build_header(pkt, 1, (unsigned)i, 1, 0, 1, 0, 0, 0, 0);
    pkt[h] = 'x';
    ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);
  }

  /* a 9th distinct segment forces eviction of slot 0 (segment_id 0) */
  {
    unsigned char pkt[12 + 1];
    size_t h = build_header(pkt, 1, 8, 1, 0, 1, 0, 0, 0, 0);
    pkt[h] = 'x';
    ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);
  }

  /* segment_id 0's remaining section can no longer complete it: its slot was evicted */
  {
    unsigned char pkt[12 + 1];
    size_t h = build_header(pkt, 1, 0, 1, 1, 1, 0, 0, 0, 0);
    pkt[h] = 'y';
    ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);
  }

  dvbstp_reasm_free(r);
}
END_TEST

static Suite *dvbstp_suite(void) {
  Suite *s = suite_create("dvbstp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_header_reads_minimal_fields);
  tcase_add_test(tc, parse_header_reads_provider_id);
  tcase_add_test(tc, parse_header_skips_private_words);
  tcase_add_test(tc, parse_header_rejects_short_buffer);
  tcase_add_test(tc, parse_header_rejects_nonzero_version);
  tcase_add_test(tc, parse_header_rejects_nonzero_compression);
  tcase_add_test(tc, parse_header_rejects_nonzero_compression_broadcast_discovery);
  tcase_add_test(tc, parse_header_accepts_compression_for_bcg_payload);
  tcase_add_test(tc, parse_header_rejects_truncated_provider_id);
  tcase_add_test(tc, parse_header_rejects_truncated_private_words);
  tcase_add_test(tc, reasm_feed_single_section_completes_immediately);
  tcase_add_test(tc, reasm_feed_reassembles_two_sections_in_order);
  tcase_add_test(tc, reasm_feed_verifies_crc_on_final_section);
  tcase_add_test(tc, reasm_feed_rejects_crc_mismatch);
  tcase_add_test(tc, reasm_feed_rejects_short_packet);
  tcase_add_test(tc, reasm_feed_rejects_section_number_past_last);
  tcase_add_test(tc, reasm_feed_rejects_last_section_number_too_large);
  tcase_add_test(tc, reasm_feed_rejects_crc_flag_on_non_final_section);
  tcase_add_test(tc, reasm_feed_ignores_duplicate_section);
  tcase_add_test(tc, reasm_feed_version_bump_resets_in_progress_slot);
  tcase_add_test(tc, reasm_feed_evicts_oldest_slot_once_all_slots_are_busy);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dvbstp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
