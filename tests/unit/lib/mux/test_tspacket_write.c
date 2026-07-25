/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/tspacket_write.h"

#define MAX_PKTS 8

static unsigned char g_pkts[MAX_PKTS][188];
static int g_count;

static void capture_cb(void *ctx, const unsigned char *pkt188) {
  (void)ctx;
  if (g_count < MAX_PKTS)
    memcpy(g_pkts[g_count], pkt188, 188);
  g_count++;
}

/* standard TS payload offset: same afc-driven algorithm psi.c/pes.c use to
 * demux real packets, used here as an independent decode oracle */
static size_t std_payload_off(const unsigned char *pkt) {
  unsigned afc = (pkt[3] >> 4) & 0x3;
  if (afc == 1)
    return 4;
  if (afc == 3)
    return 5 + (size_t)pkt[4];
  return (size_t)-1;
}

static uint64_t decode_pcr_base(const unsigned char *p) {
  return ((uint64_t)p[0] << 25) | ((uint64_t)p[1] << 17) | ((uint64_t)p[2] << 9) | ((uint64_t)p[3] << 1) | (p[4] >> 7);
}

START_TEST(ts_packet_emit_single_packet_reassembles_via_standard_offset) {
  unsigned char data[10];
  unsigned char cc = 0;
  size_t off, i;
  for (i = 0; i < sizeof data; i++)
    data[i] = (unsigned char)(0x30 + i);

  g_count = 0;
  size_t n = ts_packet_emit(0x0101, &cc, NULL, data, sizeof data, 0, 0, capture_cb, NULL);

  ck_assert_uint_eq(n, 1u);
  ck_assert_int_eq(g_count, 1);
  ck_assert_uint_eq(g_pkts[0][0], 0x47u);
  ck_assert_uint_eq((unsigned)(g_pkts[0][1] & 0x40), 0x40u); /* pusi set on first packet */
  ck_assert_uint_eq((((unsigned)g_pkts[0][1] & 0x1F) << 8) | g_pkts[0][2], 0x0101u);
  ck_assert_uint_eq((unsigned)(g_pkts[0][3] & 0x0F), 1u); /* cc bumped 0 -> 1 */

  off = std_payload_off(g_pkts[0]);
  ck_assert_uint_ne(off, (size_t)-1);
  ck_assert_uint_eq(188u - off, sizeof data); /* payload fills exactly to packet end */
  ck_assert_mem_eq(g_pkts[0] + off, data, sizeof data);
}
END_TEST

START_TEST(ts_packet_emit_splits_across_packets_with_pointer_on_first_only) {
  unsigned char data[190];
  unsigned char cc = 0;
  unsigned char rebuilt[190];
  size_t off, got = 0, i;
  const unsigned char pointer_byte = 0x00;

  for (i = 0; i < sizeof data; i++)
    data[i] = (unsigned char)(i & 0xFF);

  g_count = 0;
  size_t n = ts_packet_emit(0x0200, &cc, &pointer_byte, data, sizeof data, 0, 0, capture_cb, NULL);

  ck_assert_uint_eq(n, 2u);
  ck_assert_int_eq(g_count, 2);

  /* first packet: pusi set, pointer_byte precedes the section payload */
  ck_assert_uint_eq((unsigned)(g_pkts[0][1] & 0x40), 0x40u);
  off = std_payload_off(g_pkts[0]);
  ck_assert_uint_eq(g_pkts[0][off], pointer_byte);
  memcpy(rebuilt + got, g_pkts[0] + off + 1, 188 - off - 1);
  got += 188 - off - 1;

  /* second packet: pusi clear, no pointer byte */
  ck_assert_uint_eq((unsigned)(g_pkts[1][1] & 0x40), 0u);
  off = std_payload_off(g_pkts[1]);
  memcpy(rebuilt + got, g_pkts[1] + off, 188 - off);
  got += 188 - off;

  ck_assert_uint_eq(got, sizeof data);
  ck_assert_mem_eq(rebuilt, data, sizeof data);

  /* cc increments once per packet, 0 -> 1 -> 2 */
  ck_assert_uint_eq((unsigned)(g_pkts[0][3] & 0x0F), 1u);
  ck_assert_uint_eq((unsigned)(g_pkts[1][3] & 0x0F), 2u);
}
END_TEST

START_TEST(ts_packet_emit_first_packet_carries_pcr) {
  unsigned char data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
  unsigned char cc = 0;
  uint64_t pcr = 123456789ull;
  size_t off;

  g_count = 0;
  ts_packet_emit(0x0100, &cc, NULL, data, sizeof data, 1, pcr, capture_cb, NULL);

  ck_assert_int_eq(g_count, 1);
  ck_assert_uint_eq((unsigned)((g_pkts[0][3] >> 4) & 0x3), 3u); /* afc = adaptation + payload */
  ck_assert_uint_eq((unsigned)(g_pkts[0][5] & 0x10), 0x10u);    /* PCR_flag */
  ck_assert_uint_eq(decode_pcr_base(g_pkts[0] + 6), pcr);

  off = std_payload_off(g_pkts[0]);
  ck_assert_mem_eq(g_pkts[0] + off, data, sizeof data);
}
END_TEST

static Suite *tspacket_write_suite(void) {
  Suite *s = suite_create("tspacket_write");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ts_packet_emit_single_packet_reassembles_via_standard_offset);
  tcase_add_test(tc, ts_packet_emit_splits_across_packets_with_pointer_on_first_only);
  tcase_add_test(tc, ts_packet_emit_first_packet_carries_pcr);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tspacket_write_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
