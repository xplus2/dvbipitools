/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipifccret/ret/ret.h"
#include "lib/demux/rtcp.h"
#include "lib/demux/rtx.h"

/* channel_lookup() takes raw address bytes now, not text */
static channel_t *lookup_ip(channel_table_t *t, const char *ip, unsigned port) {
  unsigned char addr[4];
  inet_pton(AF_INET, ip, addr);
  return channel_lookup(t, AF_INET, addr, sizeof addr, port);
}

#define MAX_MC 32
#define MAX_UNI 32

static unsigned char g_mc_pkt[MAX_MC][2048];
static size_t g_mc_len[MAX_MC];
static int g_mc_dscp[MAX_MC];
static int g_mc_calls;

static unsigned char g_uni_pkt[MAX_UNI][2048];
static size_t g_uni_len[MAX_UNI];
static int g_uni_dscp[MAX_UNI];
static int g_uni_calls;
static int g_uni_fd;

static void send_mc(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)c;
  (void)user;
  if (g_mc_calls < MAX_MC) {
    memcpy(g_mc_pkt[g_mc_calls], pkt, len);
    g_mc_len[g_mc_calls] = len;
    g_mc_dscp[g_mc_calls] = dscp;
  }
  g_mc_calls++;
}

static void send_unicast(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)to;
  (void)tolen;
  (void)user;
  g_uni_fd = fd;
  if (g_uni_calls < MAX_UNI) {
    memcpy(g_uni_pkt[g_uni_calls], pkt, len);
    g_uni_len[g_uni_calls] = len;
    g_uni_dscp[g_uni_calls] = dscp;
  }
  g_uni_calls++;
}

static void reset_capture(void) {
  g_mc_calls = 0;
  g_uni_calls = 0;
}

/* RTX stream's own RTP seq, not exposed by rtx_pkt_t/rtx_parse (that only decodes OSN) */
static uint16_t rtx_wire_seq(const unsigned char *pkt) {
  return (uint16_t)((pkt[2] << 8) | pkt[3]);
}

static struct sockaddr_in make_client_addr(const char *ip, unsigned short port) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  inet_pton(AF_INET, ip, &a.sin_addr);
  return a;
}

#define TEST_DSCP 0x44 /* arbitrary, distinct from RET_DSCP_RTCP: proves mirroring, not coincidence */

/* channel with N sequential ring entries, seq base_seq..base_seq+n-1, DSCP TEST_DSCP,
   payload[0] == entry index for identification */
static channel_t *make_channel(channel_table_t **out_table, uint32_t ssrc, uint16_t base_seq, int n) {
  channel_table_t *t = channel_table_new(1, 32, 0);
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  int i;

  for (i = 0; i < n; i++) {
    unsigned char payload[4] = {(unsigned char)i, 0, 0, 0};
    channel_store(t, c, ssrc, (uint16_t)(base_seq + i), (uint32_t)i, TEST_DSCP, payload, sizeof payload);
  }
  *out_table = t;
  return c;
}

START_TEST(ret_handle_nack_repairs_primary_and_blp_bits) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xAAAA, 100, 5); /* seq 100..104 cached */
  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  rtcp_nack_t nack;
  rtx_pkt_t out;
  int i;

  (void)c;
  memset(&nack, 0, sizeof nack);
  nack.sender_ssrc = 0x1111;
  nack.media_ssrc = 0xAAAA;
  nack.entry_count = 1;
  nack.entry[0].pid = 100;
  nack.entry[0].blp = 0x0005; /* bit n = seq pid+n+1: bits 0,2 -> repair 101,103 */

  reset_capture();
  ret_handle_nack(r, &nack, 42, NULL, 0);

  /* mc: 1 Generic NACK (FF) + 3 repairs (100, 101, 103) */
  ck_assert_int_eq(g_mc_calls, 4);
  ck_assert_int_eq(g_mc_dscp[0], RET_DSCP_RTCP);
  for (i = 1; i < 4; i++)
    ck_assert_int_eq(g_mc_dscp[i], TEST_DSCP); /* mirrored from channel's stored entries, F.9 */

  /* unicast: 3 repairs, same set, same fd every time */
  ck_assert_int_eq(g_uni_calls, 3);
  ck_assert_int_eq(g_uni_fd, 42);
  for (i = 0; i < 3; i++)
    ck_assert_int_eq(g_uni_dscp[i], TEST_DSCP); /* mirrored from channel's stored entries, F.9 */

  ck_assert_int_eq(rtx_parse(g_uni_pkt[0], g_uni_len[0], 99, &out), 1);
  ck_assert_uint_eq(out.osn, 100u);
  ck_assert_int_eq(rtx_parse(g_uni_pkt[1], g_uni_len[1], 99, &out), 1);
  ck_assert_uint_eq(out.osn, 101u);
  ck_assert_int_eq(rtx_parse(g_uni_pkt[2], g_uni_len[2], 99, &out), 1);
  ck_assert_uint_eq(out.osn, 103u);

  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

static int g_nack_calls;
static rtcp_nack_t g_decoded_nack;

static void on_nack(const rtcp_nack_t *n, void *user) {
  (void)user;
  g_nack_calls++;
  g_decoded_nack = *n;
}

START_TEST(ret_handle_nack_generic_nack_matches_requested_entries) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xAAAA, 100, 2);
  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  rtcp_nack_t nack;

  (void)c;
  memset(&nack, 0, sizeof nack);
  nack.sender_ssrc = 0x2222;
  nack.media_ssrc = 0xAAAA;
  nack.entry_count = 1;
  nack.entry[0].pid = 100;
  nack.entry[0].blp = 0;

  reset_capture();
  ret_handle_nack(r, &nack, 1, NULL, 0);

  ck_assert_int_ge(g_mc_calls, 1);
  g_nack_calls = 0;
  rtcp_parse(g_mc_pkt[0], g_mc_len[0], on_nack, NULL, NULL, NULL, NULL, NULL);

  ck_assert_int_eq(g_nack_calls, 1);
  ck_assert_uint_eq(g_decoded_nack.sender_ssrc, 0x2222u);
  ck_assert_uint_eq(g_decoded_nack.media_ssrc, 0xAAAAu);
  ck_assert_uint_eq(g_decoded_nack.entry_count, 1u);
  ck_assert_uint_eq(g_decoded_nack.entry[0].pid, 100u);

  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

START_TEST(ret_handle_nack_ignores_unknown_ssrc) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xAAAA, 100, 2);
  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  rtcp_nack_t nack;

  (void)c;
  memset(&nack, 0, sizeof nack);
  nack.sender_ssrc = 1;
  nack.media_ssrc = 0xFFFF; /* no such channel */
  nack.entry_count = 1;
  nack.entry[0].pid = 100;

  reset_capture();
  ret_handle_nack(r, &nack, 1, NULL, 0);

  ck_assert_int_eq(g_mc_calls, 0);
  ck_assert_int_eq(g_uni_calls, 0);

  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

START_TEST(ret_handle_nack_skips_seq_not_in_ring) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xAAAA, 100, 2); /* only 100,101 cached */
  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  rtcp_nack_t nack;

  (void)c;
  memset(&nack, 0, sizeof nack);
  nack.sender_ssrc = 1;
  nack.media_ssrc = 0xAAAA;
  nack.entry_count = 1;
  nack.entry[0].pid = 900; /* never stored */
  nack.entry[0].blp = 0;

  reset_capture();
  ret_handle_nack(r, &nack, 1, NULL, 0);
  ck_assert_int_eq(g_mc_calls, 1); /* FF still goes out */
  ck_assert_int_eq(g_uni_calls, 0); /* no repair: nothing found in ring */
  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

START_TEST(ret_on_self_detected_gap_sends_ff_and_repairs_range) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xBBBB, 50, 5); /* seq 50..54 */
  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  rtx_pkt_t out;
  int i;

  (void)c;
  reset_capture();
  ret_on_self_detected_gap(r, 0xBBBB, 51, 53);

  /* 1 FF + 3 repairs (51,52,53), all via mc only, no unicast at all */
  ck_assert_int_eq(g_mc_calls, 4);
  ck_assert_int_eq(g_uni_calls, 0);
  ck_assert_int_eq(g_mc_dscp[0], RET_DSCP_RTCP);

  for (i = 1; i < 4; i++) {
    ck_assert_int_eq(rtx_parse(g_mc_pkt[i], g_mc_len[i], 99, &out), 1);
    ck_assert_uint_eq(out.osn, (unsigned)(50 + i));
  }

  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

START_TEST(ret_on_self_detected_gap_ignores_unknown_ssrc) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xBBBB, 50, 5);

  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  (void)c;
  reset_capture();
  ret_on_self_detected_gap(r, 0x9999, 51, 53);
  ck_assert_int_eq(g_mc_calls, 0);

  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

/* F.3.2.1: N unicast RET RTP sessions, one per HNED; each client's RTX seq space
   must be its own, unshared with any other client */
START_TEST(unicast_rtx_seq_is_independent_per_client) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xAAAA, 100, 3); /* seq 100..102 cached */
  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  struct sockaddr_in addr_a = make_client_addr("10.0.0.1", 6000);
  struct sockaddr_in addr_b = make_client_addr("10.0.0.1", 6001); /* same host, different port: different session */
  rtcp_nack_t nack;

  (void)c;
  memset(&nack, 0, sizeof nack);
  nack.sender_ssrc = 1;
  nack.media_ssrc = 0xAAAA;
  nack.entry_count = 1;
  nack.entry[0].pid = 100;
  nack.entry[0].blp = 0;

  reset_capture();
  ret_handle_nack(r, &nack, 1, (const struct sockaddr *)&addr_a, sizeof addr_a);
  ck_assert_int_eq(g_uni_calls, 1);
  ck_assert_uint_eq(rtx_wire_seq(g_uni_pkt[0]), 0u); /* client A's first repair ever */

  reset_capture();
  ret_handle_nack(r, &nack, 1, (const struct sockaddr *)&addr_a, sizeof addr_a);
  ck_assert_uint_eq(rtx_wire_seq(g_uni_pkt[0]), 1u); /* client A's second: its own counter advanced */

  reset_capture();
  ret_handle_nack(r, &nack, 1, (const struct sockaddr *)&addr_b, sizeof addr_b);
  ck_assert_uint_eq(rtx_wire_seq(g_uni_pkt[0]), 0u); /* client B: independent space, starts at 0 too */

  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

/* F.3.2.1: MC RET session (repair_range/gap path, F.5.2) is a separate RTP session
   from any unicast RET session; its seq space must not be perturbed by unicast traffic */
START_TEST(mc_and_unicast_rtx_seq_are_independent) {
  channel_table_t *t;
  channel_t *c = make_channel(&t, 0xBBBB, 50, 5); /* seq 50..54 cached */
  ret_ctx_t *r = ret_ctx_new(t, 99, 32, send_mc, send_unicast, NULL);
  struct sockaddr_in addr_a = make_client_addr("10.0.0.5", 6000);
  rtcp_nack_t nack;
  int i, mc_repair_idx;

  (void)c;
  memset(&nack, 0, sizeof nack);
  nack.sender_ssrc = 1;
  nack.media_ssrc = 0xBBBB;
  nack.entry_count = 1;
  nack.entry[0].pid = 50;
  nack.entry[0].blp = 0;

  ret_handle_nack(r, &nack, 1, (const struct sockaddr *)&addr_a, sizeof addr_a); /* mc seq 0, unicast seq 0 */

  reset_capture();
  ret_on_self_detected_gap(r, 0xBBBB, 51, 51); /* mc-only repair, F.5.2: bumps mc to next 2, unicast untouched */
  ck_assert_int_eq(g_uni_calls, 0);

  reset_capture();
  ret_handle_nack(r, &nack, 1, (const struct sockaddr *)&addr_a, sizeof addr_a); /* client A's 2nd repair, mc's 3rd */
  ck_assert_int_eq(g_uni_calls, 1);
  ck_assert_uint_eq(rtx_wire_seq(g_uni_pkt[0]), 1u); /* unicast: only this client's own 2nd repair */

  mc_repair_idx = -1;
  for (i = 0; i < g_mc_calls; i++)
    if (g_mc_dscp[i] != RET_DSCP_RTCP)
      mc_repair_idx = i;
  ck_assert_int_ge(mc_repair_idx, 0);
  ck_assert_uint_eq(rtx_wire_seq(g_mc_pkt[mc_repair_idx]), 2u); /* mc: 3rd repair ever (0, 1 via gap, 2 now) */

  ret_ctx_free(r);
  channel_table_free(t);
}
END_TEST

static Suite *ret_suite(void) {
  Suite *s = suite_create("ret");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ret_handle_nack_repairs_primary_and_blp_bits);
  tcase_add_test(tc, ret_handle_nack_generic_nack_matches_requested_entries);
  tcase_add_test(tc, ret_handle_nack_ignores_unknown_ssrc);
  tcase_add_test(tc, ret_handle_nack_skips_seq_not_in_ring);
  tcase_add_test(tc, ret_on_self_detected_gap_sends_ff_and_repairs_range);
  tcase_add_test(tc, ret_on_self_detected_gap_ignores_unknown_ssrc);
  tcase_add_test(tc, unicast_rtx_seq_is_independent_per_client);
  tcase_add_test(tc, mc_and_unicast_rtx_seq_are_independent);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ret_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
