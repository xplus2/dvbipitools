/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/radiohead/priv.h"

static void make_packet(unsigned char pkt[188], unsigned char marker) {
  memset(pkt, 0xAB, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = marker;
  pkt[3] = 0x10;
}

START_TEST(meta_cb_copies_artist_and_title_and_marks_dirty) {
  meta_state_t m;
  radio_metrics_t rm;
  memset(&m, 0, sizeof m);
  memset(&rm, 0, sizeof rm);
  m.rm = &rm;

  meta_cb(&m, "The Artist", "The Title");

  ck_assert_str_eq(m.artist, "The Artist");
  ck_assert_str_eq(m.title, "The Title");
  ck_assert_int_eq(m.dirty, 1);
  ck_assert_uint_eq(rm.metadata_updates_total, 1u);
}
END_TEST

START_TEST(meta_cb_tolerates_null_metrics) {
  meta_state_t m;
  memset(&m, 0, sizeof m);
  m.rm = NULL;
  meta_cb(&m, "A", "B");
  ck_assert_int_eq(m.dirty, 1);
}
END_TEST

START_TEST(codec_name_maps_every_known_codec) {
  ck_assert_str_eq(codec_name(SRC_MPEG_AUDIO), "mpeg-audio");
  ck_assert_str_eq(codec_name(SRC_AAC_ADTS), "aac-adts");
  ck_assert_str_eq(codec_name(SRC_AAC_LATM), "aac-latm");
}
END_TEST

START_TEST(packet_cb_batches_until_ts_per_dgram_then_flushes) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.61", 15361, NULL, 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.61", 15361, NULL, 500);
  out_ctx_t o;
  unsigned char pkt[188];
  unsigned char rbuf[4096];
  int i;
  ssize_t n;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  memset(&o, 0, sizeof o);
  o.mc = send;

  for (i = 0; i < TS_PER_DGRAM - 1; i++) {
    make_packet(pkt, (unsigned char)i);
    packet_cb(&o, pkt);
  }
  ck_assert_int_eq(o.batch_count, TS_PER_DGRAM - 1);

  make_packet(pkt, (unsigned char)(TS_PER_DGRAM - 1));
  packet_cb(&o, pkt);
  ck_assert_int_eq(o.batch_count, 0);
  ck_assert_uint_eq(o.packets, (unsigned)TS_PER_DGRAM);
  ck_assert_int_eq(o.had_error, 0);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, TS_PER_DGRAM * 188);
  for (i = 0; i < TS_PER_DGRAM; i++)
    ck_assert_uint_eq(rbuf[i * 188 + 2], (unsigned char)i);

  mcast_close(send);
  mcast_close(recv);
}
END_TEST

START_TEST(flush_batch_is_a_no_op_when_empty) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.62", 15362, NULL, 1);
  out_ctx_t o;
  ck_assert_ptr_nonnull(send);
  memset(&o, 0, sizeof o);
  o.mc = send;

  flush_batch(&o);
  ck_assert_int_eq(o.batch_count, 0);
  ck_assert_uint_eq(o.packets, 0u);
  ck_assert_int_eq(o.had_error, 0);

  mcast_close(send);
}
END_TEST

START_TEST(flush_batch_prefixes_rtp_header_when_rtp_enabled) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.63", 15363, NULL, 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.63", 15363, NULL, 500);
  rtpheader_t *rtph = rtpheader_new();
  out_ctx_t o;
  unsigned char pkt[188];
  unsigned char rbuf[4096];
  ssize_t n;
  int i;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  ck_assert_ptr_nonnull(rtph);
  memset(&o, 0, sizeof o);
  o.mc = send;
  o.rtp = 1;
  o.rtph = rtph;
  o.cur_pts = 12345;

  for (i = 0; i < TS_PER_DGRAM; i++) {
    make_packet(pkt, (unsigned char)i);
    packet_cb(&o, pkt);
  }
  ck_assert_int_eq(o.batch_count, 0);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, 12 + TS_PER_DGRAM * 188);
  ck_assert_int_eq(rbuf[0] >> 6, 2); /* RTP version 2 */
  ck_assert_uint_eq(rbuf[12], 0x47);

  rtpheader_free(rtph);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

static Suite *radiohead_suite(void) {
  Suite *s = suite_create("dipiradiohead_radiohead");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, meta_cb_copies_artist_and_title_and_marks_dirty);
  tcase_add_test(tc, meta_cb_tolerates_null_metrics);
  tcase_add_test(tc, codec_name_maps_every_known_codec);
  tcase_add_test(tc, packet_cb_batches_until_ts_per_dgram_then_flushes);
  tcase_add_test(tc, flush_batch_is_a_no_op_when_empty);
  tcase_add_test(tc, flush_batch_prefixes_rtp_header_when_rtp_enabled);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(radiohead_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
