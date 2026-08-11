/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/tvhead/priv.h"

static void make_packet(unsigned char pkt[188], unsigned char marker) {
  memset(pkt, 0xAB, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = marker;
  pkt[3] = 0x10;
}

static void init_out_ctx(out_ctx_t *o, mcast_t *mc, int rtp, rtpheader_t *rtph, bitrate_pacer_t *pacer) {
  memset(o, 0, sizeof *o);
  o->mc = mc;
  o->rtp = rtp;
  o->rtph = rtph;
  o->pacer = pacer;
}

START_TEST(packet_cb_batches_until_ts_per_dgram_then_flushes) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.51", 15351, NULL, 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.51", 15351, NULL, 500);
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  unsigned char rbuf[4096];
  int i;
  ssize_t n;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  init_out_ctx(&o, send, 0, NULL, pacer);

  for (i = 0; i < TS_PER_DGRAM - 1; i++) {
    make_packet(pkt, (unsigned char)i);
    packet_cb(&o, pkt);
  }
  ck_assert_int_eq(o.batch_count, TS_PER_DGRAM - 1);
  ck_assert_uint_eq(o.packets, (unsigned)(TS_PER_DGRAM - 1));

  make_packet(pkt, (unsigned char)(TS_PER_DGRAM - 1));
  packet_cb(&o, pkt); /* Nth packet: auto-flush */
  ck_assert_int_eq(o.batch_count, 0);
  ck_assert_uint_eq(o.packets, (unsigned)TS_PER_DGRAM);
  ck_assert_int_eq(o.had_error, 0);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, TS_PER_DGRAM * 188);
  for (i = 0; i < TS_PER_DGRAM; i++)
    ck_assert_uint_eq(rbuf[i * 188 + 2], (unsigned char)i); /* marker byte round-tripped in order */

  bitrate_pacer_free(pacer);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

START_TEST(flush_batch_is_a_no_op_when_empty) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.52", 15352, NULL, 1);
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  ck_assert_ptr_nonnull(send);
  init_out_ctx(&o, send, 0, NULL, pacer);

  flush_batch(&o); /* batch_count == 0: must not touch mc or pacer */
  ck_assert_int_eq(o.batch_count, 0);
  ck_assert_uint_eq(o.packets, 0u);
  ck_assert_int_eq(o.had_error, 0);

  bitrate_pacer_free(pacer);
  mcast_close(send);
}
END_TEST

START_TEST(flush_batch_prefixes_rtp_header_when_rtp_enabled) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.53", 15353, NULL, 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.53", 15353, NULL, 500);
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  rtpheader_t *rtph = rtpheader_new();
  out_ctx_t o;
  unsigned char pkt[188];
  unsigned char rbuf[4096];
  ssize_t n;
  int i;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  ck_assert_ptr_nonnull(rtph);
  init_out_ctx(&o, send, 1, rtph, pacer);

  for (i = 0; i < TS_PER_DGRAM; i++) {
    make_packet(pkt, (unsigned char)i);
    packet_cb(&o, pkt);
  }
  ck_assert_int_eq(o.batch_count, 0);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, 12 + TS_PER_DGRAM * 188);
  ck_assert_int_eq(rbuf[0] >> 6, 2); /* RTP version 2 */
  ck_assert_uint_eq(rbuf[12], 0x47); /* first TS sync byte right after the 12B RTP header */

  rtpheader_free(rtph);
  bitrate_pacer_free(pacer);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

START_TEST(send_null_packet_emits_a_valid_null_pid_packet) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.54", 15354, NULL, 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.54", 15354, NULL, 500);
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char rbuf[4096];
  ssize_t n;
  int i;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  init_out_ctx(&o, send, 0, NULL, pacer);

  for (i = 0; i < TS_PER_DGRAM; i++)
    send_null_packet(&o);
  ck_assert_uint_eq(o.packets, (unsigned)TS_PER_DGRAM);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, TS_PER_DGRAM * 188);
  ck_assert_uint_eq(rbuf[0], 0x47);
  ck_assert_uint_eq((unsigned)((rbuf[1] << 8 | rbuf[2]) & 0x1FFF), 0x1FFFu); /* null PID */

  bitrate_pacer_free(pacer);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

static Suite *output_suite(void) {
  Suite *s = suite_create("dipitvhead_output");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, packet_cb_batches_until_ts_per_dgram_then_flushes);
  tcase_add_test(tc, flush_batch_is_a_no_op_when_empty);
  tcase_add_test(tc, flush_batch_prefixes_rtp_header_when_rtp_enabled);
  tcase_add_test(tc, send_null_packet_emits_a_valid_null_pid_packet);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(output_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
