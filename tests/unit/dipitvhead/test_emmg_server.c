/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/cas/emmg_server.h"
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

START_TEST(channel_status_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_channel_status(buf, sizeof buf, 3, 0x4A750001, 7);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 3);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_CHANNEL_STATUS);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_CLIENT_ID, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 4);
  ck_assert_uint_eq(((unsigned)val[0] << 24) | ((unsigned)val[1] << 16) | ((unsigned)val[2] << 8) | val[3], 0x4A750001u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_CHANNEL_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 7u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_SECTION_TSPKT_FLAG, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 1);
  ck_assert_uint_eq(val[0], 0);
}
END_TEST

START_TEST(channel_status_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(emmg_build_channel_status(buf, sizeof buf, 3, 1, 1), 0u);
}
END_TEST

START_TEST(stream_status_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_status(buf, sizeof buf, 3, 0x4A750001, 7, 9, 100, 0);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_STATUS);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_STREAM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 9u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 100u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_TYPE, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 1);
  ck_assert_uint_eq(val[0], 0);
}
END_TEST

START_TEST(stream_status_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(emmg_build_stream_status(buf, sizeof buf, 3, 1, 1, 1, 1, 0), 0u);
}
END_TEST

START_TEST(stream_close_response_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_close_response(buf, sizeof buf, 2, 0x11223344, 5, 6);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 2);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_CLOSE_RESPONSE);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_STREAM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 6u);
}
END_TEST

START_TEST(stream_close_response_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(emmg_build_stream_close_response(buf, sizeof buf, 3, 1, 1, 1), 0u);
}
END_TEST

START_TEST(stream_bw_allocation_includes_bandwidth_when_present) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_bw_allocation(buf, sizeof buf, 3, 1, 1, 1, 1, 512);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_BW_ALLOCATION);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_BANDWIDTH, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 512u);
}
END_TEST

START_TEST(stream_bw_allocation_omits_bandwidth_when_absent) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_bw_allocation(buf, sizeof buf, 3, 1, 1, 1, 0, 0);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_BANDWIDTH, &val, &vlen), 0);
}
END_TEST

typedef struct {
  unsigned char data[8][32];
  unsigned short len[8];
  int count;
} collected_t;

static void collect_cb(const unsigned char *data, unsigned short len, void *user) {
  collected_t *c = user;
  ck_assert_int_lt(c->count, 8);
  memcpy(c->data[c->count], data, len);
  c->len[c->count] = len;
  c->count++;
}

START_TEST(extract_datagrams_single) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  collected_t c;
  static const unsigned char dg[] = {0xAA, 0xBB, 0xCC};

  memset(&c, 0, sizeof c);
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, EMMG_MSG_DATA_PROVISION);
  simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, (unsigned char[]){0, 0, 0, 1}, 4);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg, sizeof dg);
  size_t n = simulcrypt_writer_finish(&w);

  int rc = emmg_extract_datagrams(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, collect_cb, &c);
  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(c.count, 1);
  ck_assert_uint_eq(c.len[0], sizeof dg);
  ck_assert_mem_eq(c.data[0], dg, sizeof dg);
}
END_TEST

START_TEST(extract_datagrams_multiple_in_order) {
  unsigned char buf[128];
  simulcrypt_writer_t w;
  collected_t c;
  static const unsigned char dg1[] = {1, 2};
  static const unsigned char dg2[] = {3, 4, 5};
  static const unsigned char dg3[] = {6};

  memset(&c, 0, sizeof c);
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, EMMG_MSG_DATA_PROVISION);
  simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, (unsigned char[]){0, 0, 0, 1}, 4);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_STREAM_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg1, sizeof dg1);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg2, sizeof dg2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg3, sizeof dg3);
  size_t n = simulcrypt_writer_finish(&w);

  int rc = emmg_extract_datagrams(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, collect_cb, &c);
  ck_assert_int_eq(rc, 3);
  ck_assert_int_eq(c.count, 3);
  ck_assert_mem_eq(c.data[0], dg1, sizeof dg1);
  ck_assert_mem_eq(c.data[1], dg2, sizeof dg2);
  ck_assert_mem_eq(c.data[2], dg3, sizeof dg3);
}
END_TEST

START_TEST(extract_datagrams_rejects_malformed) {
  static const unsigned char buf[] = {0x00, 0x05, 0x00}; /* truncated tag/length */
  collected_t c;
  memset(&c, 0, sizeof c);
  int rc = emmg_extract_datagrams(buf, sizeof buf, collect_cb, &c);
  ck_assert_int_eq(rc, -1);
}
END_TEST

static Suite *emmg_server_suite(void) {
  Suite *s = suite_create("emmg_server");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, channel_status_builds_expected_fields);
  tcase_add_test(tc, channel_status_rejects_small_cap);
  tcase_add_test(tc, stream_status_builds_expected_fields);
  tcase_add_test(tc, stream_status_rejects_small_cap);
  tcase_add_test(tc, stream_close_response_builds_expected_fields);
  tcase_add_test(tc, stream_close_response_rejects_small_cap);
  tcase_add_test(tc, stream_bw_allocation_includes_bandwidth_when_present);
  tcase_add_test(tc, stream_bw_allocation_omits_bandwidth_when_absent);
  tcase_add_test(tc, extract_datagrams_single);
  tcase_add_test(tc, extract_datagrams_multiple_in_order);
  tcase_add_test(tc, extract_datagrams_rejects_malformed);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(emmg_server_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
