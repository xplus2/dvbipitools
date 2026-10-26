/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../../alloc_fail.h"
#include "lib/demux/crc32.h"
#include "lib/net/dvbstp.h"
#include "lib/net/dvbstp_seen.h"
#include "lib/net/multicast.h"

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

/* total_segment_size field of an already built header */
static void set_total(unsigned char *buf, unsigned total) {
  buf[1] = (unsigned char)((total >> 16) & 0xFF);
  buf[2] = (unsigned char)((total >> 8) & 0xFF);
  buf[3] = (unsigned char)(total & 0xFF);
}

static dvbstp_header_t make_header(unsigned payload_id, unsigned segment_id, unsigned version) {
  dvbstp_header_t h;
  memset(&h, 0, sizeof h);
  h.payload_id = payload_id;
  h.segment_id = segment_id;
  h.segment_version = version;
  return h;
}

START_TEST(already_seen_is_false_on_first_sighting) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t h = make_header(1, 2, 3);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 0);
  ck_assert_int_eq(count, 1);
}
END_TEST

START_TEST(already_seen_is_true_on_repeat) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t h = make_header(1, 2, 3);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 1);
  ck_assert_int_eq(count, 1);
}
END_TEST

START_TEST(already_seen_distinguishes_by_all_three_fields) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t a = make_header(1, 2, 3);
  dvbstp_header_t b = make_header(1, 2, 4); /* different version: replaces a */
  dvbstp_header_t c = make_header(1, 9, 3); /* different segment_id */
  dvbstp_header_t d = make_header(9, 2, 3); /* different payload_id */
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &a), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &b), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &c), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &d), 0);
  ck_assert_int_eq(count, 3);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &b), 1);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &a), 0);
  ck_assert_int_eq(count, 3);
}
END_TEST

START_TEST(already_seen_stops_recording_past_the_cap) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  int i;
  for (i = 0; i < LISTEN_SEEN_MAX + 5; i++) {
    dvbstp_header_t h = make_header((unsigned)i, 0, 0);
    ck_assert_int_eq(dvbstp_already_seen(seen, &count, &h), 0);
  }
  ck_assert_int_eq(count, LISTEN_SEEN_MAX);
}
END_TEST

START_TEST(already_seen_distinguishes_by_provider_id) {
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t a = make_header(1, 2, 3);
  dvbstp_header_t b = make_header(1, 2, 3);
  b.has_provider_id = 1;
  b.provider_id[0] = 7;
  dvbstp_header_t c = b;
  c.provider_id[0] = 8;
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &a), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &b), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &c), 0);
  ck_assert_int_eq(dvbstp_already_seen(seen, &count, &b), 1);
  ck_assert_int_eq(count, 3);
}
END_TEST

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
  ck_assert_int_eq(h.ipv6, 0);
  ck_assert_mem_eq(h.provider_id, "\xDE\xAD\xBE\xEF", 4);
}
END_TEST

/* TS 102 034 V2.1.1 figure 8a: Ver 01, 128 bit ServiceProviderID */
START_TEST(parse_header_reads_ipv6_provider_id) {
  unsigned char buf[28];
  dvbstp_header_t h;
  build_header(buf, 1, 1, 1, 0, 0, 0, 1, 0, 0);
  buf[0] |= 0x40;
  for (int i = 0; i < 16; i++) buf[12 + i] = (unsigned char)(0xA0 + i);
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 28u);
  ck_assert_uint_eq(dvbstp_parse_header(buf, 27, &h), 0u);
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 28u);
  ck_assert_int_eq(h.ipv6, 1);
  ck_assert_mem_eq(h.provider_id, buf + 12, 16);
}
END_TEST

START_TEST(parse_header_rejects_nonzero_encryption) {
  unsigned char buf[12];
  dvbstp_header_t h;
  build_header(buf, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  buf[0] |= 0x02; /* Enc 01 */
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
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

START_TEST(parse_header_rejects_undefined_version) {
  unsigned char buf[12];
  dvbstp_header_t h;
  build_header(buf, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  buf[0] |= 0x80; /* Ver 10, not defined (table 11cq) */
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
  buf[0] |= 0x40; /* Ver 11 */
  ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), 0u);
}
END_TEST

/* TS 102 034 table 12: BiM any id, gzip 0x07/0x08 only, 011..101 reserved, 110/111 defined */
START_TEST(parse_header_compression_follows_table_12) {
  static const struct {
    unsigned id;
    unsigned compr;
    int ok;
  } cases[] = {{1, 1, 1}, {2, 1, 1}, {2, 2, 0}, {1, 2, 0}, {7, 2, 1}, {8, 2, 1}, {2, 3, 0}, {7, 4, 0}, {0xA3, 5, 0}, {2, 6, 1}, {2, 7, 1}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    unsigned char buf[12];
    dvbstp_header_t h;
    build_header(buf, cases[i].id, 1, 1, 0, 0, 0, 0, 0, 0);
    buf[11] |= (unsigned char)(cases[i].compr << 5);
    ck_assert_uint_eq(dvbstp_parse_header(buf, sizeof buf, &h), cases[i].ok ? 12u : 0u);
    if (cases[i].ok) ck_assert_uint_eq(h.compr, cases[i].compr);
  }
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
  set_total(pkt, 5);
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
  set_total(pkt0, 7);
  set_total(pkt1, 7);
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
  set_total(pkt, 5);
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
  set_total(pkt, 5);
  memcpy(pkt + hdrlen, "hello", 5);
  memset(pkt + hdrlen + 5, 0, 4); /* wrong crc */

  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);

  dvbstp_reasm_free(r);
}
END_TEST

static void crc_final_first(const char *sec0, int expect_done) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char p0[12 + 3];
  unsigned char p1[12 + 3 + 4];
  const unsigned char *out_data;
  size_t out_len;
  uint32_t crc = crc32_mpeg((const unsigned char *)"abcdef", 6);
  size_t h0 = build_header(p0, 1, 1, 1, 0, 1, 0, 0, 0, 0);
  size_t h1 = build_header(p1, 1, 1, 1, 1, 1, 1, 0, 0, 0);
  set_total(p0, 6);
  set_total(p1, 6);
  memcpy(p0 + h0, sec0, 3);
  memcpy(p1 + h1, "def", 3);
  p1[h1 + 3] = (unsigned char)((crc >> 24) & 0xFF);
  p1[h1 + 4] = (unsigned char)((crc >> 16) & 0xFF);
  p1[h1 + 5] = (unsigned char)((crc >> 8) & 0xFF);
  p1[h1 + 6] = (unsigned char)(crc & 0xFF);

  ck_assert_int_eq(dvbstp_reasm_feed(r, p1, h1 + 7, NULL, &out_data, &out_len), 0);
  ck_assert_int_eq(dvbstp_reasm_feed(r, p0, h0 + 3, NULL, &out_data, &out_len), expect_done);
  if (expect_done) ck_assert_uint_eq(out_len, 6u);
  dvbstp_reasm_free(r);
}

START_TEST(reasm_feed_accepts_good_crc_when_final_section_first) {
  crc_final_first("abc", 1);
}
END_TEST

START_TEST(reasm_feed_rejects_bad_crc_when_final_section_first) {
  crc_final_first("abX", 0);
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

START_TEST(reasm_feed_assembles_more_than_64_sections) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  const unsigned char *out_data = NULL;
  size_t out_len = 0;
  int done = 0;

  for (unsigned i = 0; i < 300; i++) {
    unsigned char pkt[12 + 2];
    size_t h = build_header(pkt, 1, 1, 1, 299 - i, 299, 0, 0, 0, 0); /* reverse order */
    set_total(pkt, 600);
    pkt[h] = (unsigned char)('a' + i % 26);
    pkt[h + 1] = (unsigned char)i;
    done = dvbstp_reasm_feed(r, pkt, h + 2, NULL, &out_data, &out_len);
    ck_assert_int_eq(done, i == 299 ? 1 : 0);
  }
  ck_assert_uint_eq(out_len, 600u);
  ck_assert_int_eq(out_data[0], 'a' + 299 % 26);
  ck_assert_int_eq(out_data[598], 'a');
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_accepts_last_section_4095) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt[12 + 1];
  const unsigned char *out_data;
  size_t out_len;
  size_t hdrlen = build_header(pkt, 1, 1, 1, 0, 4095, 0, 0, 0, 0);
  pkt[hdrlen] = 'x';
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, sizeof pkt, NULL, &out_data, &out_len), 0);
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_accepts_section_above_recommended_size) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  static unsigned char pkt[12 + 8000];
  const unsigned char *out_data;
  size_t out_len;
  size_t h = build_header(pkt, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  set_total(pkt, 8000);
  memset(pkt + h, 'x', 8000);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, h + 8000, NULL, &out_data, &out_len), 1);
  ck_assert_uint_eq(out_len, 8000u);
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_section_over_rx_cap) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  static unsigned char pkt[12 + DVBSTP_MAX_RX_SECTION + 1];
  const unsigned char *out_data;
  size_t out_len;
  size_t h = build_header(pkt, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  memset(pkt + h, 'x', DVBSTP_MAX_RX_SECTION + 1);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt, h + DVBSTP_MAX_RX_SECTION + 1, NULL, &out_data, &out_len), 0);
  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_segment_over_byte_cap) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  static unsigned char pkt[12 + DVBSTP_MAX_SECTION];
  const unsigned char *out_data;
  size_t out_len;
  unsigned n = DVBSTP_MAX_SEGMENT / DVBSTP_MAX_SECTION + 2;
  int done = 0;

  for (unsigned i = 0; i < n; i++) {
    size_t h = build_header(pkt, 1, 1, 1, i, n, 0, 0, 0, 0);
    memset(pkt + h, 'x', DVBSTP_MAX_SECTION);
    done |= dvbstp_reasm_feed(r, pkt, h + DVBSTP_MAX_SECTION, NULL, &out_data, &out_len);
  }
  ck_assert_int_eq(done, 0);
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
  set_total(pkt0, 7);
  set_total(pkt1, 7);
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
  set_total(pkt_v1, 7);
  memcpy(pkt_v1 + h, "old", 3);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt_v1, sizeof pkt_v1, NULL, &out_data, &out_len), 0);

  h = build_header(pkt_v2_0, 1, 1, 2, 0, 1, 0, 0, 0, 0); /* version bumped to 2 */
  set_total(pkt_v2_0, 7);
  memcpy(pkt_v2_0 + h, "new", 3);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt_v2_0, sizeof pkt_v2_0, NULL, &out_data, &out_len), 0);

  h = build_header(pkt_v2_1, 1, 1, 2, 1, 1, 0, 0, 0, 0);
  set_total(pkt_v2_1, 7);
  memcpy(pkt_v2_1 + h, "data", 4);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt_v2_1, sizeof pkt_v2_1, NULL, &out_data, &out_len), 1);
  ck_assert_uint_eq(out_len, 7u);
  ck_assert_mem_eq(out_data, "newdata", 7); /* v1's section carried over: v2's data only */

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_rejects_total_segment_size_mismatch) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char pkt0[12 + 3];
  unsigned char pkt1[12 + 4];
  const unsigned char *out_data;
  size_t out_len;
  size_t h0 = build_header(pkt0, 1, 1, 1, 0, 1, 0, 0, 0, 0);
  size_t h1 = build_header(pkt1, 1, 1, 1, 1, 1, 0, 0, 0, 0);
  memcpy(pkt0 + h0, "abc", 3);
  memcpy(pkt1 + h1, "defg", 4);
  set_total(pkt0, 8);
  set_total(pkt1, 8);

  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt0, sizeof pkt0, NULL, &out_data, &out_len), 0);
  ck_assert_int_eq(dvbstp_reasm_feed(r, pkt1, sizeof pkt1, NULL, &out_data, &out_len), 0);

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_keeps_providers_apart) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  unsigned char a0[16 + 3];
  unsigned char a1[16 + 4];
  unsigned char b0[16 + 3];
  unsigned char b1[16 + 4];
  const unsigned char *out_data;
  size_t out_len;
  size_t h;

  h = build_header(a0, 1, 1, 1, 0, 1, 0, 1, 0xAAAA, 0);
  set_total(a0, 7);
  memcpy(a0 + h, "aaa", 3);
  h = build_header(a1, 1, 1, 1, 1, 1, 0, 1, 0xAAAA, 0);
  set_total(a1, 7);
  memcpy(a1 + h, "AAAA", 4);
  h = build_header(b0, 1, 1, 1, 0, 1, 0, 1, 0xBBBB, 0);
  set_total(b0, 7);
  memcpy(b0 + h, "bbb", 3);
  h = build_header(b1, 1, 1, 1, 1, 1, 0, 1, 0xBBBB, 0);
  set_total(b1, 7);
  memcpy(b1 + h, "BBBB", 4);

  ck_assert_int_eq(dvbstp_reasm_feed(r, a0, sizeof a0, NULL, &out_data, &out_len), 0);
  ck_assert_int_eq(dvbstp_reasm_feed(r, b0, sizeof b0, NULL, &out_data, &out_len), 0);
  ck_assert_int_eq(dvbstp_reasm_feed(r, a1, sizeof a1, NULL, &out_data, &out_len), 1);
  ck_assert_mem_eq(out_data, "aaaAAAA", 7);
  ck_assert_int_eq(dvbstp_reasm_feed(r, b1, sizeof b1, NULL, &out_data, &out_len), 1);
  ck_assert_mem_eq(out_data, "bbbBBBB", 7);

  dvbstp_reasm_free(r);
}
END_TEST

START_TEST(reasm_feed_evicts_oldest_slot_once_all_slots_are_busy) {
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  const unsigned char *out_data;
  size_t out_len;

  /* fill all 8 slots with incomplete two-section segments (segment_id 0..7) */
  for (int i = 0; i < 8; i++) {
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

static int send_and_collect(int family, const char *dest, size_t seglen, size_t *max_dgram, int *count, dvbstp_header_t *first) {
  struct sockaddr_storage ss;
  struct timeval tv = {0, 200000};
  socklen_t sl = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
  unsigned char *seg = calloc(1, seglen);
  unsigned char buf[2048];
  dvbstp_send_t desc = {.payload_id = DVBSTP_PAYLOAD_SP_DISCOVERY, .segment_id = 1, .has_provider_id = 1, .provider_id = {7}, .want_crc = 1};
  int rx = socket(family, SOCK_DGRAM, 0);
  unsigned port;
  mcast_t *m;
  ssize_t n;
  int rc;

  *max_dgram = 0;
  *count = 0;
  memset(&ss, 0, sizeof ss);
  if (family == AF_INET) {
    ((struct sockaddr_in *)&ss)->sin_family = AF_INET;
    inet_pton(AF_INET, dest, &((struct sockaddr_in *)&ss)->sin_addr);
  } else {
    ((struct sockaddr_in6 *)&ss)->sin6_family = AF_INET6;
    inet_pton(AF_INET6, dest, &((struct sockaddr_in6 *)&ss)->sin6_addr);
  }
  if (rx < 0 || bind(rx, (struct sockaddr *)&ss, sl) < 0 || getsockname(rx, (struct sockaddr *)&ss, &sl) < 0) {
    if (rx >= 0) close(rx);
    free(seg);
    return -1;
  }
  setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  port = ntohs(family == AF_INET ? ((struct sockaddr_in *)&ss)->sin_port : ((struct sockaddr_in6 *)&ss)->sin6_port);
  m = mcast_open_send(family, dest, port, NULL, 0);
  ck_assert_ptr_nonnull(m);
  rc = dvbstp_send_segment(m, &desc, seg, seglen);
  ck_assert_int_eq(rc, 0);
  while ((n = recv(rx, buf, sizeof buf, 0)) > 0) {
    if ((size_t)n > *max_dgram) *max_dgram = (size_t)n;
    if (!(*count)++) ck_assert_uint_gt(dvbstp_parse_header(buf, (size_t)n, first), 0);
  }
  mcast_close(m);
  close(rx);
  free(seg);
  return 0;
}

START_TEST(send_segment_v4_fills_sections_to_ipv4_limit) {
  size_t max;
  int count;
  dvbstp_header_t h;
  if (send_and_collect(AF_INET, "127.0.0.1", 3 * DVBSTP_MAX_SECTION, &max, &count, &h) < 0) return;
  ck_assert_int_eq(count, 3);
  ck_assert_uint_eq(max, 16 + DVBSTP_MAX_SECTION + 4);
  ck_assert_int_eq(h.ipv6, 0);
  ck_assert_uint_eq(h.provider_id[0], 7);
}
END_TEST

START_TEST(send_segment_v6_keeps_datagrams_within_1452) {
  size_t max;
  int count;
  dvbstp_header_t h;
  if (send_and_collect(AF_INET6, "::1", 3 * DVBSTP_MAX_SECTION, &max, &count, &h) < 0) return;
  ck_assert_int_eq(count, 4);
  ck_assert_uint_le(max, 1452u);
  ck_assert_uint_eq(max, 28 + DVBSTP_MAX_SECTION_V6); /* full non-final section, no crc */
  ck_assert_int_eq(h.ipv6, 1);
  ck_assert_uint_eq(h.provider_id[0], 7);
}
END_TEST

static long send_elapsed_ms(int64_t budget_ns) {
  dvbstp_send_t desc = {.payload_id = DVBSTP_PAYLOAD_SP_DISCOVERY, .segment_id = 1, .want_crc = 1, .pacing_budget_ns = budget_ns};
  size_t seglen = 100 * DVBSTP_MAX_SECTION;
  unsigned char *seg = calloc(1, seglen);
  struct timespec t0;
  struct timespec t1;
  mcast_t *m = mcast_open_send(AF_INET, "127.0.0.1", 9, NULL, 0);
  ck_assert_ptr_nonnull(m);
  clock_gettime(CLOCK_MONOTONIC, &t0);
  ck_assert_int_eq(dvbstp_send_segment(m, &desc, seg, seglen), 0);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  mcast_close(m);
  free(seg);
  return (t1.tv_sec - t0.tv_sec) * 1000L + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
}

START_TEST(send_segment_default_gap_is_1ms_per_section) {
  ck_assert_int_ge(send_elapsed_ms(0), 99);
}
END_TEST

START_TEST(send_segment_pacing_budget_caps_total_delay) {
  ck_assert_int_lt(send_elapsed_ms(10000000L), 60);
}
END_TEST

START_TEST(reasm_new_and_feed_survive_allocation_failures) {
  unsigned char pkts[3][12 + 6];
  unsigned char crc_pkt[12 + 6 + 4];
  const unsigned char *out_data;
  size_t out_len;
  dvbstp_reasm_t *r;
  alloc_fail_at(0);
  r = dvbstp_reasm_new();
  alloc_fail_end();
  ck_assert_ptr_null(r);
  for (int i = 0; i < 3; i++) {
    size_t h = build_header(pkts[i], 1, 5, 1, (unsigned)i, 2, 0, 0, 0, 0);
    set_total(pkts[i], 18);
    memcpy(pkts[i] + h, "abcdef", 6);
  }
  {
    size_t h = build_header(crc_pkt, 1, 6, 1, 0, 0, 1, 0, 0, 0);
    uint32_t crc;
    set_total(crc_pkt, 6);
    memcpy(crc_pkt + h, "abcdef", 6);
    crc = crc32_mpeg(crc_pkt + h, 6);
    crc_pkt[h + 6] = (unsigned char)(crc >> 24);
    crc_pkt[h + 7] = (unsigned char)(crc >> 16);
    crc_pkt[h + 8] = (unsigned char)(crc >> 8);
    crc_pkt[h + 9] = (unsigned char)crc;
  }
  for (long n = 0;; n++) {
    int hit;
    r = dvbstp_reasm_new();
    ck_assert_ptr_nonnull(r);
    alloc_fail_at(n);
    for (int i = 0; i < 3; i++) (void)dvbstp_reasm_feed(r, pkts[i], sizeof pkts[i], NULL, &out_data, &out_len);
    (void)dvbstp_reasm_feed(r, crc_pkt, sizeof crc_pkt, NULL, &out_data, &out_len);
    hit = alloc_fail_end();
    dvbstp_reasm_free(r);
    if (!hit) break;
  }
  for (long n = 0;; n++) {
    int hit;
    r = dvbstp_reasm_new();
    ck_assert_ptr_nonnull(r);
    for (int i = 0; i < 2; i++) ck_assert_int_eq(dvbstp_reasm_feed(r, pkts[i], sizeof pkts[i], NULL, &out_data, &out_len), 0);
    alloc_fail_at(n);
    (void)dvbstp_reasm_feed(r, pkts[2], sizeof pkts[2], NULL, &out_data, &out_len);
    hit = alloc_fail_end();
    dvbstp_reasm_free(r);
    if (!hit) break;
  }
}
END_TEST

typedef struct {
  int calls;
  int stop_after;
  size_t last_len;
} listen_ctx_t;

static int on_segment(void *ctx, const dvbstp_header_t *h, const unsigned char *data, size_t len) {
  listen_ctx_t *c = ctx;
  (void)h;
  (void)data;
  c->calls++;
  c->last_len = len;
  return c->stop_after && c->calls >= c->stop_after;
}

static void send_segments(const char *group, unsigned port, int n) {
  struct sockaddr_in dst;
  int tx = socket(AF_INET, SOCK_DGRAM, 0);
  ck_assert_int_ge(tx, 0);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &dst.sin_addr), 1);
  for (int i = 0; i < n; i++) {
    unsigned char pkt[12 + 5];
    size_t h = build_header(pkt, 1, (unsigned)i + 1, 1, 0, 0, 0, 0, 0, 0);
    set_total(pkt, 5);
    memcpy(pkt + h, "hello", 5);
    ck_assert_int_eq((int)sendto(tx, pkt, sizeof pkt, 0, (struct sockaddr *)&dst, sizeof dst), (int)sizeof pkt);
  }
  ck_assert_int_eq((int)sendto(tx, "x", 1, 0, (struct sockaddr *)&dst, sizeof dst), 1);
  close(tx);
}

static unsigned pick_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;
  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

START_TEST(listen_delivers_segments_until_callback_stops_or_time_runs_out) {
  char group[32];
  unsigned port = pick_port();
  listen_ctx_t c = {0, 0, 0};
  mcast_t *m;
  snprintf(group, sizeof group, "239.77.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
  m = mcast_open(AF_INET, group, port, NULL, 100);
  ck_assert_ptr_nonnull(m);
  send_segments(group, port, 3);
  c.stop_after = 2;
  ck_assert_int_eq(dvbstp_listen(m, 5.0, on_segment, &c), 0);
  ck_assert_int_eq(c.calls, 2);
  ck_assert_uint_eq(c.last_len, 5u);
  c.stop_after = 0;
  ck_assert_int_eq(dvbstp_listen(m, 0.3, on_segment, &c), 0);
  ck_assert_int_eq(c.calls, 3);
  mcast_close(m);
}
END_TEST

START_TEST(listen_gives_up_after_repeated_receive_errors) {
  char group[32];
  unsigned port = pick_port();
  listen_ctx_t c = {0, 0, 0};
  mcast_t *m;
  snprintf(group, sizeof group, "239.78.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
  m = mcast_open(AF_INET, group, port, NULL, 50);
  ck_assert_ptr_nonnull(m);
  close(mcast_fd(m));
  ck_assert_int_eq(dvbstp_listen(m, 30.0, on_segment, &c), -1);
  ck_assert_int_eq(c.calls, 0);
  free(m);
}
END_TEST

START_TEST(listen_fails_without_memory) {
  char group[32];
  unsigned port = pick_port();
  listen_ctx_t c = {0, 0, 0};
  mcast_t *m;
  int rc;
  snprintf(group, sizeof group, "239.79.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
  m = mcast_open(AF_INET, group, port, NULL, 50);
  ck_assert_ptr_nonnull(m);
  alloc_fail_at(0);
  rc = dvbstp_listen(m, 1.0, on_segment, &c);
  alloc_fail_end();
  ck_assert_int_eq(rc, -2);
  mcast_close(m);
}
END_TEST

static Suite *dvbstp_suite(void) {
  Suite *s = suite_create("dvbstp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_header_reads_minimal_fields);
  tcase_add_test(tc, parse_header_reads_provider_id);
  tcase_add_test(tc, parse_header_skips_private_words);
  tcase_add_test(tc, parse_header_rejects_short_buffer);
  tcase_add_test(tc, parse_header_rejects_undefined_version);
  tcase_add_test(tc, parse_header_reads_ipv6_provider_id);
  tcase_add_test(tc, parse_header_rejects_nonzero_encryption);
  tcase_add_test(tc, parse_header_compression_follows_table_12);
  tcase_add_test(tc, parse_header_accepts_compression_for_bcg_payload);
  tcase_add_test(tc, parse_header_rejects_truncated_provider_id);
  tcase_add_test(tc, parse_header_rejects_truncated_private_words);
  tcase_add_test(tc, reasm_feed_single_section_completes_immediately);
  tcase_add_test(tc, reasm_feed_reassembles_two_sections_in_order);
  tcase_add_test(tc, reasm_feed_verifies_crc_on_final_section);
  tcase_add_test(tc, reasm_feed_rejects_crc_mismatch);
  tcase_add_test(tc, reasm_feed_accepts_good_crc_when_final_section_first);
  tcase_add_test(tc, reasm_feed_rejects_bad_crc_when_final_section_first);
  tcase_add_test(tc, reasm_feed_rejects_short_packet);
  tcase_add_test(tc, reasm_feed_rejects_section_number_past_last);
  tcase_add_test(tc, reasm_feed_assembles_more_than_64_sections);
  tcase_add_test(tc, reasm_feed_accepts_last_section_4095);
  tcase_add_test(tc, reasm_feed_accepts_section_above_recommended_size);
  tcase_add_test(tc, reasm_feed_rejects_section_over_rx_cap);
  tcase_add_test(tc, reasm_feed_rejects_segment_over_byte_cap);
  tcase_add_test(tc, reasm_feed_rejects_crc_flag_on_non_final_section);
  tcase_add_test(tc, reasm_feed_ignores_duplicate_section);
  tcase_add_test(tc, reasm_feed_version_bump_resets_in_progress_slot);
  tcase_add_test(tc, reasm_feed_rejects_total_segment_size_mismatch);
  tcase_add_test(tc, reasm_feed_keeps_providers_apart);
  tcase_add_test(tc, reasm_feed_evicts_oldest_slot_once_all_slots_are_busy);
  tcase_add_test(tc, send_segment_v4_fills_sections_to_ipv4_limit);
  tcase_add_test(tc, send_segment_v6_keeps_datagrams_within_1452);
  tcase_add_test(tc, send_segment_default_gap_is_1ms_per_section);
  tcase_add_test(tc, send_segment_pacing_budget_caps_total_delay);
  tcase_add_test(tc, reasm_new_and_feed_survive_allocation_failures);
  tcase_add_test(tc, listen_delivers_segments_until_callback_stops_or_time_runs_out);
  tcase_add_test(tc, listen_gives_up_after_repeated_receive_errors);
  tcase_add_test(tc, listen_fails_without_memory);
  tcase_add_test(tc, already_seen_is_false_on_first_sighting);
  tcase_add_test(tc, already_seen_is_true_on_repeat);
  tcase_add_test(tc, already_seen_distinguishes_by_all_three_fields);
  tcase_add_test(tc, already_seen_stops_recording_past_the_cap);
  tcase_add_test(tc, already_seen_distinguishes_by_provider_id);
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
