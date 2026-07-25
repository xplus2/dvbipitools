/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipifccret/channel.h"
#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"

static void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  size_t i;
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

/* video-pid packet with adaptation_field random_access_indicator set */
static void build_rai_packet(unsigned char pkt[188], unsigned pid) {
  memset(pkt, 0xCD, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x00 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x30; /* afc = adaptation + payload */
  pkt[4] = 0x01; /* adaptation_field_length */
  pkt[5] = 0x40; /* random_access_indicator */
}

static size_t build_pat_pmt_rai(unsigned char *out, unsigned prog_num, unsigned pmt_pid, unsigned video_pid) {
  unsigned char sec[64];
  size_t slen, off = 0;

  slen = psi_build_pat(0x1234, 0, prog_num, pmt_pid, sec, sizeof sec);
  wrap_section_packet(out + off, 0x0000, sec, slen);
  off += 188;
  {
    unsigned char body[16];
    size_t n = 0, hdr, crc_at;
    uint32_t crc;
    body[n++] = (unsigned char)(prog_num >> 8);
    body[n++] = (unsigned char)prog_num;
    body[n++] = 0xC1;
    body[n++] = 0x00;
    body[n++] = 0x00;
    body[n++] = 0xE0 | ((video_pid >> 8) & 0x1F);
    body[n++] = (unsigned char)video_pid;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    body[n++] = 0x1B; /* H264 */
    body[n++] = 0xE0 | ((video_pid >> 8) & 0x1F);
    body[n++] = (unsigned char)video_pid;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    hdr = n + 4;
    sec[0] = 0x02;
    sec[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
    sec[2] = (unsigned char)hdr;
    memcpy(sec + 3, body, n);
    crc_at = 3 + n;
    crc = crc32_mpeg(sec, crc_at);
    sec[crc_at + 0] = (unsigned char)(crc >> 24);
    sec[crc_at + 1] = (unsigned char)(crc >> 16);
    sec[crc_at + 2] = (unsigned char)(crc >> 8);
    sec[crc_at + 3] = (unsigned char)crc;
    slen = crc_at + 4;
  }
  wrap_section_packet(out + off, pmt_pid, sec, slen);
  off += 188;
  build_rai_packet(out + off, video_pid);
  off += 188;
  return off;
}

START_TEST(channel_lookup_allocates_finds_and_exhausts) {
  channel_table_t *t = channel_table_new(2, 0, 0);
  channel_t *a, *b, *a_again, *c;
  a = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  ck_assert_ptr_nonnull(a);
  b = channel_lookup(t, AF_INET, "239.1.1.2", 5000);
  ck_assert_ptr_nonnull(b);
  ck_assert_ptr_ne(a, b);
  a_again = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  ck_assert_ptr_eq(a, a_again);
  c = channel_lookup(t, AF_INET, "239.1.1.3", 5000); /* table only has 2 slots */
  ck_assert_ptr_null(c);

  channel_table_free(t);
}
END_TEST

START_TEST(channel_store_and_find_ret_ring_round_trips) {
  channel_table_t *t = channel_table_new(1, 4, 0); /* RET only, 4 slots */
  channel_t *c = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  unsigned char payload[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  channel_slot_t out;
  channel_store(c, 0xAABBCCDD, 100, 900000, payload, sizeof payload);
  ck_assert_int_eq(channel_find(c, 100, &out), 1);
  ck_assert_uint_eq(out.seq, 100u);
  ck_assert_uint_eq(out.timestamp, 900000u);
  ck_assert_uint_eq(out.payload_len, sizeof payload);
  ck_assert_mem_eq(out.payload, payload, sizeof payload);
  ck_assert_int_eq(out.valid, 1);
  ck_assert_int_eq(channel_find(c, 999, &out), 0); /* never stored */
  channel_table_free(t);
}
END_TEST

START_TEST(channel_find_returns_zero_when_ring_disabled) {
  channel_table_t *t = channel_table_new(1, 0, 0); /* no RET */
  channel_t *c = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  channel_slot_t out;
  unsigned char payload[4] = {1, 2, 3, 4};
  channel_store(c, 1, 1, 1, payload, sizeof payload);
  ck_assert_int_eq(channel_find(c, 1, &out), 0);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_ring_wraps_and_overwrites_oldest) {
  channel_table_t *t = channel_table_new(1, 2, 0); /* only 2 ring slots */
  channel_t *c = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  unsigned char p1[1] = {1}, p2[1] = {2}, p3[1] = {3};
  channel_slot_t out;
  channel_store(c, 1, 10, 0, p1, 1); /* slot 10%2=0 */
  channel_store(c, 1, 11, 0, p2, 1); /* slot 11%2=1 */
  channel_store(c, 1, 12, 0, p3, 1); /* slot 12%2=0, overwrites seq 10's slot */
  ck_assert_int_eq(channel_find(c, 10, &out), 0); /* overwritten: slot now holds seq 12 */
  ck_assert_int_eq(channel_find(c, 11, &out), 1);
  ck_assert_int_eq(channel_find(c, 12, &out), 1);
  ck_assert_uint_eq(out.payload[0], 3u);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_find_by_ssrc_locates_the_right_channel) {
  channel_table_t *t = channel_table_new(2, 1, 0);
  channel_t *a = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  channel_t *b = channel_lookup(t, AF_INET, "239.1.1.2", 5000);
  unsigned char payload[1] = {0};
  channel_store(a, 0x1111, 1, 0, payload, 1);
  channel_store(b, 0x2222, 1, 0, payload, 1);
  ck_assert_ptr_eq(channel_find_by_ssrc(t, 0x1111), a);
  ck_assert_ptr_eq(channel_find_by_ssrc(t, 0x2222), b);
  ck_assert_ptr_null(channel_find_by_ssrc(t, 0x9999));
  channel_table_free(t);
}
END_TEST

START_TEST(channel_fcc_cache_tracks_rap_and_entries) {
  channel_table_t *t = channel_table_new(1, 0, 8); /* FCC only, 8-entry cache */
  channel_t *c = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  unsigned char discovery[3 * 188];
  unsigned char plain[188];
  size_t dlen;

  ck_assert_int_eq(channel_has_rap(c), 0);
  ck_assert_uint_eq(channel_cache_count(c), 0u);

  /* before any RAP: stored but not cached */
  memset(plain, 0xAB, sizeof plain);
  plain[0] = 0x47;
  channel_store(c, 1, 1, 0, plain, sizeof plain);
  ck_assert_int_eq(channel_has_rap(c), 0);
  ck_assert_uint_eq(channel_cache_count(c), 0u);
  dlen = build_pat_pmt_rai(discovery, 101, 0x0100, 0x0101);
  channel_store(c, 1, 2, 0, discovery, dlen); /* PAT+PMT+RAI video in one call */
  ck_assert_int_eq(channel_has_rap(c), 1);
  ck_assert_uint_eq(channel_cache_count(c), 1u); /* the RAP entry itself */
  channel_store(c, 1, 3, 0, plain, sizeof plain);
  ck_assert_uint_eq(channel_cache_count(c), 2u);
  {
    rap_cache_entry_t e;
    ck_assert_int_eq(channel_cache_get(c, 0, &e), 1);
    ck_assert_uint_eq(e.seq, 2u); /* the RAP-bearing store */
    ck_assert_int_eq(channel_cache_get(c, 1, &e), 1);
    ck_assert_uint_eq(e.seq, 3u);
    ck_assert_int_eq(channel_cache_get(c, 2, &e), 0); /* nothing there yet */
  }
  channel_table_free(t);
}
END_TEST

START_TEST(channel_has_rap_stays_zero_when_fcc_disabled) {
  channel_table_t *t = channel_table_new(1, 0, 0); /* cache_cap 0: FCC off */
  channel_t *c = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  unsigned char discovery[3 * 188];
  size_t dlen = build_pat_pmt_rai(discovery, 101, 0x0100, 0x0101);
  channel_store(c, 1, 1, 0, discovery, dlen);
  ck_assert_int_eq(channel_has_rap(c), 0);
  ck_assert_uint_eq(channel_cache_count(c), 0u);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_table_reap_frees_stale_channels) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *a, *b;
  a = channel_lookup(t, AF_INET, "239.1.1.1", 5000);
  ck_assert_ptr_nonnull(a);
  channel_table_reap(t, -1); /* max_age -1: always "older" than now */
  b = channel_lookup(t, AF_INET, "239.1.1.2", 5000); /* slot should be free again */
  ck_assert_ptr_nonnull(b);
  ck_assert_ptr_eq(a, b); /* same underlying slot, reused */
  channel_table_free(t);
}
END_TEST

static Suite *channel_suite(void) {
  Suite *s = suite_create("channel");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, channel_lookup_allocates_finds_and_exhausts);
  tcase_add_test(tc, channel_store_and_find_ret_ring_round_trips);
  tcase_add_test(tc, channel_find_returns_zero_when_ring_disabled);
  tcase_add_test(tc, channel_ring_wraps_and_overwrites_oldest);
  tcase_add_test(tc, channel_find_by_ssrc_locates_the_right_channel);
  tcase_add_test(tc, channel_fcc_cache_tracks_rap_and_entries);
  tcase_add_test(tc, channel_has_rap_stays_zero_when_fcc_disabled);
  tcase_add_test(tc, channel_table_reap_frees_stale_channels);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(channel_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
