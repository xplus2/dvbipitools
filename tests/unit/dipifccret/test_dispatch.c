/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "dipifccret/run/run.h"
#include "lib/demux/rtx.h"
#include "lib/mux/rtcp_build.h"

#define RTX_PT 99
#define MEDIA_SSRC 0xABCD0001u
#define HNED_SSRC 0x11110001u
#define SECOND_SSRC 0x22220002u
#define MAX_CAP 16
#define PKT_LEN 188
#define BASE_SEQ 100

typedef struct {
  channel_table_t *ch;
  ret_ctx_t *ret;
  burst_table_t *bursts;
  dispatch_ctx_t d;
  ret_send_ctx_t rsc;
  int srv;
  int cli[2];
  struct sockaddr_in cli_addr[2];
  channel_t *chan;
} fx_t;

typedef struct {
  rtcp_rams_i_t rams_i[MAX_CAP];
  int n_rams_i;
  uint16_t rtx_osn[MAX_CAP];
  uint16_t rtx_seq[MAX_CAP];
  int n_rtx;
} replies_t;

static unsigned char g_mc_pkt[MAX_CAP][2048];
static size_t g_mc_len[MAX_CAP];
static int g_mc_calls;

static void mc_record(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)c;
  (void)dscp;
  (void)user;
  if (g_mc_calls < MAX_CAP) {
    memcpy(g_mc_pkt[g_mc_calls], pkt, len);
    g_mc_len[g_mc_calls] = len;
  }
  g_mc_calls++;
}

static int udp_bound(struct sockaddr_in *addr_out) {
  socklen_t len = sizeof *addr_out;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  memset(addr_out, 0, sizeof *addr_out);
  addr_out->sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr_out->sin_addr);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)addr_out, sizeof *addr_out), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)addr_out, &len), 0);
  return fd;
}

static channel_t *chan_at(channel_table_t *t, const char *ip, unsigned port) {
  unsigned char a[4];

  inet_pton(AF_INET, ip, a);
  return channel_lookup(t, AF_INET, a, sizeof a, port);
}

static void fx_open(fx_t *fx, int with_ret, int with_bursts) {
  struct sockaddr_in srv_addr;

  memset(fx, 0, sizeof *fx);
  g_mc_calls = 0;
  fx->ch = channel_table_new(8, 64, 64);
  ck_assert_ptr_nonnull(fx->ch);
  fx->d.channels = fx->ch;
  fx->d.rtx_pt = RTX_PT;
  fx->d.burst_multiplier = 2.0;
  fx->d.duration_cap_ms = 1000;
  fx->rsc.last_dscp = -1;
  if (with_ret) {
    fx->ret = ret_ctx_new(fx->ch, RTX_PT, 8, mc_record, ret_send_unicast_impl, &fx->rsc);
    ck_assert_ptr_nonnull(fx->ret);
    fx->d.ret = fx->ret;
  }
  if (with_bursts) {
    fx->bursts = burst_table_new(2);
    ck_assert_ptr_nonnull(fx->bursts);
    fx->d.bursts = fx->bursts;
  }
  fx->srv = udp_bound(&srv_addr);
  fx->cli[0] = udp_bound(&fx->cli_addr[0]);
  fx->cli[1] = udp_bound(&fx->cli_addr[1]);
}

static void fx_close(fx_t *fx) {
  close(fx->srv);
  close(fx->cli[0]);
  close(fx->cli[1]);
  ret_ctx_free(fx->ret);
  burst_table_free(fx->bursts);
  channel_table_free(fx->ch);
}

static void fx_fill_channel(fx_t *fx, size_t n, int rap) {
  unsigned char payload[PKT_LEN];

  fx->chan = chan_at(fx->ch, "239.1.1.1", 5000);
  ck_assert_ptr_nonnull(fx->chan);
  if (rap) atomic_store(&fx->chan->cache.have_rap, 1);
  for (size_t i = 0; i < n; i++) {
    memset(payload, (int)(0x10 + i), sizeof payload);
    payload[0] = 0x47;
    channel_store(fx->ch, fx->chan, MEDIA_SSRC, (uint16_t)(BASE_SEQ + i), (uint32_t)(i * 45000u), 0x28, payload, sizeof payload);
  }
  atomic_store(&fx->chan->nominal_bps, 1000000.0);
}

static void fx_send_as(fx_t *fx, int who, const unsigned char *pkt, size_t len) {
  listen_cb(pkt, len, fx->srv, (const struct sockaddr *)&fx->cli_addr[who], sizeof fx->cli_addr[who], &fx->d);
}

static void fx_send(fx_t *fx, const unsigned char *pkt, size_t len) {
  fx_send_as(fx, 0, pkt, len);
}

static void rams_i_collect(const rtcp_rams_i_t *info, void *user) {
  replies_t *r = user;
  if (r->n_rams_i < MAX_CAP)
    r->rams_i[r->n_rams_i] = *info;
  r->n_rams_i++;
}

static void fx_drain_as(const fx_t *fx, int who, replies_t *r) {
  unsigned char buf[2048];
  ssize_t n;

  memset(r, 0, sizeof *r);
  while ((n = recv(fx->cli[who], buf, sizeof buf, MSG_DONTWAIT)) > 0) {
    if (buf[1] >= 192 && buf[1] <= 223) {
      rtcp_parse(buf, (size_t)n, &(rtcp_cbs_t){.rams_i_cb = rams_i_collect, .user = r});
    } else {
      rtx_pkt_t rx;

      ck_assert(rtx_parse(buf, (size_t)n, RTX_PT, &rx));
      if (r->n_rtx < MAX_CAP) {
        r->rtx_osn[r->n_rtx] = rx.osn;
        r->rtx_seq[r->n_rtx] = (uint16_t)((buf[2] << 8) | buf[3]);
      }
      r->n_rtx++;
    }
  }
}

static void fx_drain(fx_t *fx, replies_t *r) {
  fx_drain_as(fx, 0, r);
}

static size_t build_nack(unsigned char *out, uint32_t sender, uint32_t media, uint16_t pid, uint16_t blp) {
  rtcp_nack_entry_t e = {pid, blp};

  return rtcp_build_ff(sender, media, &e, 1, out, 128);
}

static size_t build_rams_r(unsigned char *out, const rtcp_rams_r_t *req) {
  return rtcp_build_rams_r(req, out, 128);
}

static rtcp_rams_r_t rams_r_for(uint32_t sender, uint32_t media) {
  rtcp_rams_r_t r;

  memset(&r, 0, sizeof r);
  r.sender_ssrc = sender;
  r.media_ssrc = media;
  return r;
}

static size_t build_sdes(unsigned char *out, uint32_t ssrc, const char *cname) {
  size_t clen = strlen(cname);
  size_t body = 4 + 2 + clen + 1;
  size_t padded = (body + 3) & ~(size_t)3;
  size_t total = 4 + padded;

  memset(out, 0, total);
  out[0] = 0x81;
  out[1] = 202;
  out[2] = (unsigned char)((total / 4 - 1) >> 8);
  out[3] = (unsigned char)(total / 4 - 1);
  out[4] = (unsigned char)(ssrc >> 24);
  out[5] = (unsigned char)(ssrc >> 16);
  out[6] = (unsigned char)(ssrc >> 8);
  out[7] = (unsigned char)ssrc;
  out[8] = 1;
  out[9] = (unsigned char)clen;
  memcpy(out + 10, cname, clen);
  return total;
}

static size_t collisions_of(fx_t *fx, uint32_t *out, size_t cap) {
  return channel_hned_collisions(fx->chan, out, cap, 60);
}

START_TEST(nack_replies_unicast_and_repairs_via_multicast) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtx_pkt_t rx;
  size_t n;

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 10, 0);
  n = build_nack(pkt, HNED_SSRC, MEDIA_SSRC, 102, 0x0005);
  fx_send(&fx, pkt, n);

  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rtx, 3);
  ck_assert_uint_eq(r.rtx_osn[0], 102u);
  ck_assert_uint_eq(r.rtx_osn[1], 103u);
  ck_assert_uint_eq(r.rtx_osn[2], 105u);
  ck_assert_int_eq(g_mc_calls, 4);
  ck_assert(!rtx_parse(g_mc_pkt[0], g_mc_len[0], RTX_PT, &rx));
  ck_assert_uint_eq(g_mc_pkt[0][1], 205u);
  ck_assert(rtx_parse(g_mc_pkt[1], g_mc_len[1], RTX_PT, &rx));
  ck_assert_uint_eq(rx.osn, 102u);
  fx_close(&fx);
}
END_TEST

START_TEST(nack_for_unknown_ssrc_is_ignored) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  size_t n;

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 4, 0);
  n = build_nack(pkt, HNED_SSRC, 0xDEAD, 100, 0);
  fx_send(&fx, pkt, n);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rtx, 0);
  ck_assert_int_eq(g_mc_calls, 0);
  fx_close(&fx);
}
END_TEST

START_TEST(truncated_nack_logs_once_and_rearms_after_a_normal_nack) {
  fx_t fx;
  unsigned char pkt[12 + 4 * (RTCP_NACK_MAX_ENTRIES + 1)];
  unsigned char small[128];
  rtcp_nack_entry_t entries[RTCP_NACK_MAX_ENTRIES + 1];
  char log[LOG_CAPTURE_BUF];
  size_t n;
  size_t ns;
  size_t i;

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 4, 0);
  for (i = 0; i < RTCP_NACK_MAX_ENTRIES + 1; i++) {
    entries[i].pid = (uint16_t)(1000 + i);
    entries[i].blp = 0;
  }
  n = rtcp_build_ff(HNED_SSRC, MEDIA_SSRC, entries, RTCP_NACK_MAX_ENTRIES + 1, pkt, sizeof pkt);
  ns = build_nack(small, HNED_SSRC, MEDIA_SSRC, 100, 0);
  ck_assert_uint_gt(n, 0u);

  log_capture_begin();
  fx_send(&fx, pkt, n);
  fx_send(&fx, pkt, n);
  fx_send(&fx, small, ns);
  fx_send(&fx, pkt, n);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(log_count_of(log, "dropping the rest"), 2);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_without_fcc_gets_504) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);

  fx_open(&fx, 0, 0);
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_NOT_SUPPORTED);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_from_client_outside_client_range_gets_505) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  cidr_t range;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  ck_assert_int_eq(cidr_parse("10.0.0.0/8", &range), 0);
  fx.d.fcc_client_ranges = &range;
  fx.d.fcc_client_range_count = 1;
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_NOT_ELIGIBLE);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_for_channel_outside_fcc_range_gets_506) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  cidr_t range;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  ck_assert_int_eq(cidr_parse("10.0.0.0/8", &range), 0);
  fx.d.fcc_ranges = &range;
  fx.d.fcc_range_count = 1;
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_NOT_ENABLED);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_from_client_inside_client_range_is_accepted) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  cidr_t range;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  ck_assert_int_eq(cidr_parse("127.0.0.0/8", &range), 0);
  fx.d.fcc_client_ranges = &range;
  fx.d.fcc_client_range_count = 1;
  fx.d.fcc_ranges = &range;
  fx.d.fcc_range_count = 0;
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_ACCEPT);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_decide_rejections_map_to_their_codes) {
  struct {
    int rap;
    int unknown_ssrc;
    int ignore_ssrc;
    int has_min;
    unsigned min;
    int has_max;
    unsigned max;
    int has_bps;
    uint64_t bps;
    unsigned bound;
    unsigned expect;
  } cases[] = {
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, BURST_SSRC_NOT_FOUND},
    {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, BURST_REJECT},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, BURST_NO_RAP},
    {1, 0, 0, 1, 5000, 0, 0, 0, 0, 0, BURST_NO_VALID_START},
    {1, 0, 0, 1, 900, 1, 100, 0, 0, 0, BURST_MALFORMED},
    {1, 0, 0, 1, 900, 0, 0, 0, 0, 500, BURST_MIN_BUFFER_INVALID},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, BURST_MAX_BUFFER_INVALID},
    {1, 0, 0, 0, 0, 0, 0, 1, 1000, 0, BURST_BITRATE_INSUFFICIENT},
  };
  size_t i;

  for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    fx_t fx;
    unsigned char pkt[128];
    replies_t r;
    rtcp_rams_r_t req = rams_r_for(HNED_SSRC, cases[i].unknown_ssrc ? 0xDEAD : MEDIA_SSRC);

    fx_open(&fx, 0, 1);
    fx_fill_channel(&fx, 3, cases[i].rap);
    fx.d.max_buffer_fill_bound_ms = cases[i].bound;
    req.ignore_media_ssrc = cases[i].ignore_ssrc;
    req.has_min_buffer_fill = cases[i].has_min;
    req.min_buffer_fill_ms = cases[i].min;
    req.has_max_buffer_fill = cases[i].has_max;
    req.max_buffer_fill_ms = cases[i].max;
    req.has_max_bitrate = cases[i].has_bps;
    req.max_bitrate_bps = cases[i].bps;
    size_t plen = build_rams_r(pkt, &req);
    fx_send(&fx, pkt, plen);
    fx_drain(&fx, &r);
    ck_assert_msg(r.n_rams_i == 1, "case %zu", i);
    ck_assert_msg(r.rams_i[0].response == cases[i].expect, "case %zu got %u", i, (unsigned)r.rams_i[0].response);
    fx_close(&fx);
  }
}
END_TEST

START_TEST(rams_r_accept_carries_rams_i_tlvs_and_repeat_gets_update) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  burst_slot_t *slot;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  fx.d.duration_cap_ms = 4321;
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_ACCEPT);
  ck_assert_uint_eq(r.rams_i[0].msn, 0u);
  ck_assert_int_eq(r.rams_i[0].has_first_packet_seqnum, 1);
  ck_assert_uint_eq(r.rams_i[0].first_packet_seqnum, (unsigned)BASE_SEQ);
  ck_assert_int_eq(r.rams_i[0].has_earliest_join_time, 1);
  ck_assert_uint_eq(r.rams_i[0].earliest_join_time_ms, 0u);
  ck_assert_int_eq(r.rams_i[0].has_burst_duration, 1);
  ck_assert_uint_eq(r.rams_i[0].burst_duration_ms, 4321u);
  ck_assert_int_eq(r.rams_i[0].has_max_transmit_bitrate, 1);
  ck_assert_uint_eq(r.rams_i[0].max_transmit_bitrate_bps, 2000000u);
  slot = burst_table_find(fx.bursts, (struct sockaddr *)&fx.cli_addr[0], sizeof fx.cli_addr[0]);
  ck_assert_ptr_nonnull(slot);

  plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_UPDATE);
  ck_assert_uint_eq(r.rams_i[0].msn, 1u);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_client_bitrate_cap_lowers_the_burst_rate) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  req.has_max_bitrate = 1;
  req.max_bitrate_bps = 1500000;
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_ACCEPT);
  ck_assert_uint_eq(r.rams_i[0].max_transmit_bitrate_bps, 1500000u);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_with_full_burst_table_gets_503) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  struct sockaddr_in extra;
  int fds[4];
  size_t i;
  int answered_503 = 0;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  for (i = 0; i < 4; i++) {
    fds[i] = udp_bound(&extra);
    size_t plen = build_rams_r(pkt, &req);
    listen_cb(pkt, plen, fx.srv, (struct sockaddr *)&extra, sizeof extra, &fx.d);
    {
      unsigned char buf[256];
      rtcp_cbs_t cbs;
      replies_t rr;

      memset(&rr, 0, sizeof rr);
      memset(&cbs, 0, sizeof cbs);
      cbs.rams_i_cb = rams_i_collect;
      cbs.user = &rr;
      ssize_t n = recv(fds[i], buf, sizeof buf, MSG_DONTWAIT);
      ck_assert_int_gt(n, 0);
      rtcp_parse(buf, (size_t)n, &cbs);
      if (rr.n_rams_i == 1 && rr.rams_i[0].response == BURST_TABLE_FULL)
        answered_503 = 1;
    }
  }
  fx_drain(&fx, &r);
  ck_assert_int_eq(answered_503, 1);
  for (i = 0; i < 4; i++)
    close(fds[i]);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_ignore_media_ssrc_uses_the_resolved_channel) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, 0);

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  req.ignore_media_ssrc = 1;
  size_t plen = build_rams_r(pkt, &req);
  listen_resolve_cb(pkt, plen, fx.chan->resolve_slot, fx.srv, (struct sockaddr *)&fx.cli_addr[0], sizeof fx.cli_addr[0], &fx.d);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_ACCEPT);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_with_unclaimed_resolve_slot_falls_back_to_ssrc) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t by_ssrc = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  rtcp_rams_r_t ignoring = rams_r_for(HNED_SSRC, 0);
  size_t other_slot;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  other_slot = (fx.chan->resolve_slot + 1) % channel_table_capacity(fx.ch);
  ck_assert_ptr_null(channel_lookup_by_resolve_slot(fx.ch, other_slot));
  size_t plen = build_rams_r(pkt, &by_ssrc);
  listen_resolve_cb(pkt, plen, other_slot, fx.srv, (struct sockaddr *)&fx.cli_addr[0], sizeof fx.cli_addr[0], &fx.d);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_ACCEPT);

  ignoring.ignore_media_ssrc = 1;
  plen = build_rams_r(pkt, &ignoring);
  listen_resolve_cb(pkt, plen, other_slot, fx.srv, (struct sockaddr *)&fx.cli_addr[1], sizeof fx.cli_addr[1], &fx.d);
  fx_drain_as(&fx, 1, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_REJECT);
  fx_close(&fx);
}
END_TEST

static size_t build_rams_t(unsigned char *out, uint32_t sender, uint32_t media, int has_seq, uint32_t seq) {
  rtcp_rams_t_t t;

  memset(&t, 0, sizeof t);
  t.sender_ssrc = sender;
  t.media_ssrc = media;
  t.has_first_mc_seqnum = has_seq;
  t.first_mc_seqnum = seq;
  return rtcp_build_rams_t(&t, out, 128);
}

static burst_t *burst_of(fx_t *fx, int who) {
  burst_slot_t *s = burst_table_find(fx->bursts, (struct sockaddr *)&fx->cli_addr[who], sizeof fx->cli_addr[who]);

  return s ? atomic_load(&s->b) : NULL;
}

START_TEST(rams_t_without_seq_stops_the_burst_now) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(burst_is_done(burst_of(&fx, 0)), 0);
  plen = build_rams_t(pkt, HNED_SSRC, MEDIA_SSRC, 0, 0);
  fx_send(&fx, pkt, plen);
  ck_assert_int_eq(burst_is_done(burst_of(&fx, 0)), 1);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 0);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_t_with_first_mc_seq_sets_the_stop_sequence) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  burst_t *b;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  size_t plen = build_rams_r(pkt, &req);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  plen = build_rams_t(pkt, HNED_SSRC, MEDIA_SSRC, 1, 0x10002);
  fx_send(&fx, pkt, plen);
  b = burst_of(&fx, 0);
  ck_assert_int_eq(burst_is_done(b), 0);
  ck_assert_int_eq(atomic_load(&b->has_stop_seq), 1);
  ck_assert_uint_eq(atomic_load(&b->stop_seq), 2u);
  fx_close(&fx);
}
END_TEST

START_TEST(malformed_rams_t_gets_404_reply) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  size_t n = build_rams_t(pkt, HNED_SSRC, MEDIA_SSRC, 1, 7);

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  pkt[18] = 0x01;
  pkt[19] = 0x00;
  fx_send(&fx, pkt, n);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_TERM_MALFORMED);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_t_without_bursts_is_ignored) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;
  size_t n = build_rams_t(pkt, HNED_SSRC, MEDIA_SSRC, 1, 7);

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 3, 0);
  fx_send(&fx, pkt, n);
  pkt[18] = 0x01;
  pkt[19] = 0x00;
  fx_send(&fx, pkt, n);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 0);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_t_without_active_session_does_nothing) {
  fx_t fx;
  unsigned char pkt[128];
  replies_t r;

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  size_t plen = build_rams_t(pkt, HNED_SSRC, MEDIA_SSRC, 0, 0);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 0);
  fx_close(&fx);
}
END_TEST

static void start_burst_and_nack(fx_t *fx, unsigned threshold, int nacks, replies_t *last) {
  unsigned char pkt[128];
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);

  fx_open(fx, 1, 1);
  fx_fill_channel(fx, 6, 1);
  fx->d.congestion_nack_threshold = threshold;
  size_t plen = build_rams_r(pkt, &req);
  fx_send(fx, pkt, plen);
  fx_drain(fx, last);
  ck_assert_uint_eq(last->rams_i[0].response, (unsigned)BURST_ACCEPT);
  for (int i = 0; i < nacks; i++) {
    plen = build_nack(pkt, HNED_SSRC, MEDIA_SSRC, 100, 0);
    fx_send(fx, pkt, plen);
  }
}

START_TEST(nack_threshold_halves_the_rate_then_terminates) {
  fx_t fx;
  replies_t r;
  burst_t *b;
  unsigned char pkt[128];
  double initial;

  start_burst_and_nack(&fx, 4, 0, &r);
  b = burst_of(&fx, 0);
  initial = atomic_load(&b->target_bps);

  size_t plen = build_nack(pkt, HNED_SSRC, MEDIA_SSRC, 100, 0);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 0);

  plen = build_nack(pkt, HNED_SSRC, MEDIA_SSRC, 100, 0);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_UPDATE);
  ck_assert_uint_eq(r.rams_i[0].msn, 1u);
  ck_assert_int_eq(r.rams_i[0].has_max_transmit_bitrate, 1);
  ck_assert_uint_eq(r.rams_i[0].max_transmit_bitrate_bps, (uint64_t)(initial * 0.5));
  ck_assert_double_eq(atomic_load(&b->target_bps), initial * 0.5);

  plen = build_nack(pkt, HNED_SSRC, MEDIA_SSRC, 100, 0);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 0);
  ck_assert_int_eq(burst_is_done(b), 0);

  plen = build_nack(pkt, HNED_SSRC, MEDIA_SSRC, 100, 0);
  fx_send(&fx, pkt, plen);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_CONGESTION);
  ck_assert_int_eq(burst_is_done(b), 1);
  fx_close(&fx);
}
END_TEST

START_TEST(nack_threshold_zero_disables_congestion_handling) {
  fx_t fx;
  replies_t r;

  start_burst_and_nack(&fx, 0, 10, &r);
  fx_drain(&fx, &r);
  ck_assert_int_eq(r.n_rams_i, 0);
  ck_assert_int_eq(burst_is_done(burst_of(&fx, 0)), 0);
  fx_close(&fx);
}
END_TEST

START_TEST(nack_from_client_without_burst_is_not_counted) {
  fx_t fx;
  replies_t r;
  unsigned char pkt[128];

  start_burst_and_nack(&fx, 1, 0, &r);
  size_t plen = build_nack(pkt, SECOND_SSRC, MEDIA_SSRC, 100, 0);
  fx_send_as(&fx, 1, pkt, plen);
  fx_drain_as(&fx, 1, &r);
  ck_assert_int_eq(r.n_rams_i, 0);
  ck_assert_int_eq(burst_is_done(burst_of(&fx, 0)), 0);
  fx_close(&fx);
}
END_TEST

START_TEST(sdes_and_nack_in_either_order_track_the_cname) {
  fx_t fx;
  unsigned char nack[64];
  unsigned char sdes[64];
  unsigned char both[160];
  uint32_t out[8];
  size_t nn;
  size_t sn;

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 4, 0);
  fx.d.rsi_active = 1;
  nn = build_nack(nack, HNED_SSRC, MEDIA_SSRC, 100, 0);
  sn = build_sdes(sdes, HNED_SSRC, "hned-a");

  memcpy(both, sdes, sn);
  memcpy(both + sn, nack, nn);
  fx_send(&fx, both, sn + nn);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 0u);

  build_sdes(sdes, HNED_SSRC, "hned-b");
  memcpy(both, nack, nn);
  memcpy(both + nn, sdes, sn);
  fx_send_as(&fx, 0, both, nn + sn);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 1u);
  ck_assert_uint_eq(out[0], HNED_SSRC);
  fx_close(&fx);
}
END_TEST

START_TEST(same_cname_from_other_address_is_no_collision_but_no_cname_is) {
  fx_t fx;
  unsigned char nack[64];
  unsigned char sdes[64];
  unsigned char both[160];
  uint32_t out[8];
  size_t nn;
  size_t sn;

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 4, 0);
  fx.d.rsi_active = 1;
  nn = build_nack(nack, HNED_SSRC, MEDIA_SSRC, 100, 0);
  sn = build_sdes(sdes, HNED_SSRC, "same");
  memcpy(both, sdes, sn);
  memcpy(both + sn, nack, nn);
  fx_send_as(&fx, 0, both, sn + nn);
  fx_send_as(&fx, 1, both, sn + nn);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 0u);

  fx_send_as(&fx, 0, nack, nn);
  fx_send_as(&fx, 1, nack, nn);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 1u);
  fx_close(&fx);
}
END_TEST

START_TEST(hned_tracking_is_off_when_rsi_is_inactive) {
  fx_t fx;
  unsigned char nack[64];
  uint32_t out[8];
  size_t nn;

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 4, 0);
  nn = build_nack(nack, HNED_SSRC, MEDIA_SSRC, 100, 0);
  fx_send_as(&fx, 0, nack, nn);
  fx_send_as(&fx, 1, nack, nn);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 0u);
  fx_close(&fx);
}
END_TEST

START_TEST(rams_r_and_rams_t_also_track_hned_addresses) {
  fx_t fx;
  unsigned char pkt[128];
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  uint32_t out[8];

  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  fx.d.rsi_active = 1;
  size_t plen = build_rams_r(pkt, &req);
  fx_send_as(&fx, 0, pkt, plen);
  plen = build_rams_r(pkt, &req);
  fx_send_as(&fx, 1, pkt, plen);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 1u);

  plen = build_rams_t(pkt, SECOND_SSRC, MEDIA_SSRC, 0, 0);
  fx_send_as(&fx, 0, pkt, plen);
  plen = build_rams_t(pkt, SECOND_SSRC, MEDIA_SSRC, 0, 0);
  fx_send_as(&fx, 1, pkt, plen);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 2u);
  fx_close(&fx);
}
END_TEST

START_TEST(resolve_callback_attributes_hned_to_the_resolved_channel) {
  fx_t fx;
  unsigned char nack[64];
  uint32_t out[8];
  size_t nn;

  fx_open(&fx, 1, 0);
  fx_fill_channel(&fx, 4, 0);
  fx.d.rsi_active = 1;
  nn = build_nack(nack, HNED_SSRC, 0x5151, 100, 0);
  listen_resolve_cb(nack, nn, fx.chan->resolve_slot, fx.srv, (struct sockaddr *)&fx.cli_addr[0], sizeof fx.cli_addr[0], &fx.d);
  listen_resolve_cb(nack, nn, fx.chan->resolve_slot, fx.srv, (struct sockaddr *)&fx.cli_addr[1], sizeof fx.cli_addr[1], &fx.d);
  ck_assert_uint_eq(collisions_of(&fx, out, 8), 1u);
  fx_close(&fx);
}
END_TEST

START_TEST(capture_cb_stores_packets_and_provisions_multicast_sockets) {
  fx_t fx;
  unsigned char addr[4] = {239, 1, 1, 1};
  unsigned char payload[PKT_LEN];
  channel_t *c;
  mcsend_table_t *mt = mcsend_table_new(8, NULL, 1);
  mcsend_table_t *rsi_mt = mcsend_table_new(8, NULL, 1);
  channel_slot_t slot;

  fx_open(&fx, 1, 0);
  memset(payload, 0x5A, sizeof payload);
  payload[0] = 0x47;
  fx.d.mt = mt;
  fx.d.rsi_mt = rsi_mt;
  fx.d.ff_port = 0;
  capture_cb(AF_INET, addr, sizeof addr, 5000, 0x28, MEDIA_SSRC, 7, 1234, payload, sizeof payload, &fx.d);
  c = channel_find_by_ssrc(fx.ch, MEDIA_SSRC);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(channel_find(c, 7, &slot), 1);
  ck_assert_ptr_nonnull(mcsend_get(mt, c));
  ck_assert_ptr_nonnull(mcsend_get(rsi_mt, c));
  mcsend_table_free(mt);
  mcsend_table_free(rsi_mt);
  fx_close(&fx);
}
END_TEST

START_TEST(capture_cb_rejects_new_channels_beyond_the_cap) {
  fx_t fx;
  unsigned char addr[4] = {239, 1, 1, 0};
  unsigned char payload[PKT_LEN];
  unsigned i;
  size_t used = 0;
  size_t k;

  fx_open(&fx, 1, 0);
  memset(payload, 0x5A, sizeof payload);
  for (i = 0; i < 12; i++) {
    addr[3] = (unsigned char)(10 + i);
    capture_cb(AF_INET, addr, sizeof addr, 5000, 0, 0x100 + i, 1, 0, payload, sizeof payload, &fx.d);
  }
  for (k = 0; k < channel_table_capacity(fx.ch); k++)
    if (channel_table_at(fx.ch, k))
      used++;
  ck_assert_uint_eq(used, 8u);
  fx_close(&fx);
}
END_TEST

START_TEST(capture_cb_reaps_idle_channels_step_by_step) {
  fx_t fx;
  unsigned char a1[4] = {239, 1, 1, 1};
  unsigned char a2[4] = {239, 1, 1, 2};
  unsigned char payload[PKT_LEN];
  channel_t *old;

  fx_open(&fx, 0, 0);
  memset(payload, 0x5A, sizeof payload);
  capture_cb(AF_INET, a1, sizeof a1, 5000, 0, 0x201, 1, 0, payload, sizeof payload, &fx.d);
  old = channel_find_by_ssrc(fx.ch, 0x201);
  ck_assert_ptr_nonnull(old);
  atomic_store(&old->last_seen, time(NULL) - 1000);
  fx.d.idle_timeout_s = 10;
  capture_cb(AF_INET, a2, sizeof a2, 5000, 0, 0x202, 1, 0, payload, sizeof payload, &fx.d);
  ck_assert_ptr_null(channel_find_by_ssrc(fx.ch, 0x201));
  ck_assert_ptr_nonnull(channel_find_by_ssrc(fx.ch, 0x202));
  fx_close(&fx);
}
END_TEST

START_TEST(capture_cb_reaps_idle_ret_client_sessions) {
  fx_t fx;
  unsigned char addr[4] = {239, 1, 1, 1};
  unsigned char payload[PKT_LEN];
  unsigned char pkt[128];
  struct timespec t0;
  struct timespec now;

  fx_open(&fx, 1, 0);
  memset(payload, 0x5A, sizeof payload);
  capture_cb(AF_INET, addr, sizeof addr, 5000, 0, MEDIA_SSRC, 100, 0, payload, sizeof payload, &fx.d);
  size_t plen = build_nack(pkt, HNED_SSRC, MEDIA_SSRC, 100, 0);
  fx_send(&fx, pkt, plen);
  ck_assert_uint_eq(ret_ctx_active_clients(fx.ret), 1u);
  fx.d.ret_client_idle_timeout_s = 1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  do {
    usleep(50000);
    capture_cb(AF_INET, addr, sizeof addr, 5000, 0, MEDIA_SSRC, 101, 0, payload, sizeof payload, &fx.d);
    clock_gettime(CLOCK_MONOTONIC, &now);
  } while (ret_ctx_active_clients(fx.ret) != 0 && now.tv_sec - t0.tv_sec < 5);
  ck_assert_uint_eq(ret_ctx_active_clients(fx.ret), 0u);
  fx_close(&fx);
}
END_TEST

START_TEST(capture_cb_feeds_the_inspector_when_enabled) {
  fx_t fx;
  unsigned char addr[4] = {239, 1, 1, 1};
  unsigned char payload[PKT_LEN];
  tsinspect_agg_t *agg = tsinspect_agg_new(METRICS_INSPECT_TS_BASIC);
  channel_t *c;

  fx_open(&fx, 0, 0);
  memset(payload, 0x5A, sizeof payload);
  payload[0] = 0x47;
  fx.d.agg = agg;
  capture_cb(AF_INET, addr, sizeof addr, 5000, 0, MEDIA_SSRC, 1, 0, payload, sizeof payload, &fx.d);
  c = channel_find_by_ssrc(fx.ch, MEDIA_SSRC);
  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_nonnull(c->insp);
  capture_cb(AF_INET, addr, sizeof addr, 5000, 0, MEDIA_SSRC, 2, 0, payload, sizeof payload, &fx.d);
  fx_close(&fx);
  tsinspect_agg_free(agg);
}
END_TEST

START_TEST(ret_send_mc_impl_sends_only_when_the_channel_socket_exists) {
  struct sockaddr_in rx_addr;
  int rx = udp_bound(&rx_addr);
  channel_table_t *ch = channel_table_new(4, 8, 0);
  mcsend_table_t *mt = mcsend_table_new(4, NULL, 1);
  ret_send_ctx_t ctx = {NULL, -1};
  unsigned char addr[4] = {127, 0, 0, 1};
  unsigned char buf[16];
  channel_t *c = channel_lookup(ch, AF_INET, addr, sizeof addr, ntohs(rx_addr.sin_port));

  ck_assert_ptr_nonnull(c);
  ret_send_mc_impl(c, (const unsigned char *)"a", 1, 0, &ctx);
  ctx.mt = mt;
  ret_send_mc_impl(c, (const unsigned char *)"b", 1, 0, &ctx);
  ck_assert_int_eq(recv(rx, buf, sizeof buf, MSG_DONTWAIT), -1);
  mcsend_ensure(mt, c, 0);
  ret_send_mc_impl(c, (const unsigned char *)"c", 1, 0x28, &ctx);
  ck_assert_int_eq(recv(rx, buf, sizeof buf, MSG_DONTWAIT), 1);
  ck_assert_uint_eq(buf[0], 'c');
  mcsend_table_free(mt);
  channel_table_free(ch);
  close(rx);
}
END_TEST

START_TEST(burst_send_cb_flags_congestion_only_for_would_block_errors) {
  struct sockaddr_in to;
  int fd = udp_bound(&to);
  unicast_dest_t dst;
  unsigned char b[4] = {1, 2, 3, 4};

  dst.fd = fd;
  dst.to = (const struct sockaddr *)&to;
  dst.tolen = sizeof to;
  dst.congestion = 0;
  dst.last_dscp = -1;
  burst_send_cb(b, sizeof b, 0x28, &dst);
  ck_assert_int_eq(dst.congestion, 0);
  ck_assert_int_eq(dst.last_dscp, 0x28);
  close(fd);
  burst_send_cb(b, sizeof b, 0x28, &dst);
  ck_assert_int_eq(dst.congestion, 0);
}
END_TEST

START_TEST(ipv6_client_is_matched_against_ranges_and_gets_replies) {
  fx_t fx;
  unsigned char pkt[128];
  rtcp_rams_r_t req = rams_r_for(HNED_SSRC, MEDIA_SSRC);
  struct sockaddr_in6 a6;
  socklen_t len = sizeof a6;
  cidr_t range;
  unsigned char buf[256];
  replies_t r;
  int c6 = socket(AF_INET6, SOCK_DGRAM, 0);
  int s6 = socket(AF_INET6, SOCK_DGRAM, 0);
  ssize_t n;

  if (c6 < 0 || s6 < 0)
    return;
  memset(&a6, 0, sizeof a6);
  a6.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &a6.sin6_addr);
  if (bind(c6, (struct sockaddr *)&a6, sizeof a6) < 0 || getsockname(c6, (struct sockaddr *)&a6, &len) < 0) {
    close(c6);
    close(s6);
    return;
  }
  fx_open(&fx, 0, 1);
  fx_fill_channel(&fx, 3, 1);
  ck_assert_int_eq(cidr_parse("::1/128", &range), 0);
  fx.d.fcc_client_ranges = &range;
  fx.d.fcc_client_range_count = 1;
  size_t plen = build_rams_r(pkt, &req);
  listen_cb(pkt, plen, s6, (struct sockaddr *)&a6, sizeof a6, &fx.d);
  n = recv(c6, buf, sizeof buf, MSG_DONTWAIT);
  ck_assert_int_gt(n, 0);
  memset(&r, 0, sizeof r);
  rtcp_parse(buf, (size_t)n, &(rtcp_cbs_t){.rams_i_cb = rams_i_collect, .user = &r});
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_ACCEPT);

  ck_assert_int_eq(cidr_parse("2001:db8::/32", &range), 0);
  plen = build_rams_r(pkt, &req);
  listen_cb(pkt, plen, s6, (struct sockaddr *)&a6, sizeof a6, &fx.d);
  n = recv(c6, buf, sizeof buf, MSG_DONTWAIT);
  memset(&r, 0, sizeof r);
  rtcp_parse(buf, (size_t)n, &(rtcp_cbs_t){.rams_i_cb = rams_i_collect, .user = &r});
  ck_assert_uint_eq(r.rams_i[0].response, (unsigned)BURST_NOT_ELIGIBLE);
  close(c6);
  close(s6);
  fx_close(&fx);
}
END_TEST

static Suite *dispatch_suite(void) {
  Suite *s = suite_create("dipifccret_dispatch");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 15);
  tcase_add_test(tc, nack_replies_unicast_and_repairs_via_multicast);
  tcase_add_test(tc, nack_for_unknown_ssrc_is_ignored);
  tcase_add_test(tc, truncated_nack_logs_once_and_rearms_after_a_normal_nack);
  tcase_add_test(tc, rams_r_without_fcc_gets_504);
  tcase_add_test(tc, rams_r_from_client_outside_client_range_gets_505);
  tcase_add_test(tc, rams_r_for_channel_outside_fcc_range_gets_506);
  tcase_add_test(tc, rams_r_from_client_inside_client_range_is_accepted);
  tcase_add_test(tc, rams_r_decide_rejections_map_to_their_codes);
  tcase_add_test(tc, rams_r_accept_carries_rams_i_tlvs_and_repeat_gets_update);
  tcase_add_test(tc, rams_r_client_bitrate_cap_lowers_the_burst_rate);
  tcase_add_test(tc, rams_r_with_full_burst_table_gets_503);
  tcase_add_test(tc, rams_r_ignore_media_ssrc_uses_the_resolved_channel);
  tcase_add_test(tc, rams_r_with_unclaimed_resolve_slot_falls_back_to_ssrc);
  tcase_add_test(tc, rams_t_without_seq_stops_the_burst_now);
  tcase_add_test(tc, rams_t_with_first_mc_seq_sets_the_stop_sequence);
  tcase_add_test(tc, malformed_rams_t_gets_404_reply);
  tcase_add_test(tc, rams_t_without_bursts_is_ignored);
  tcase_add_test(tc, rams_t_without_active_session_does_nothing);
  tcase_add_test(tc, nack_threshold_halves_the_rate_then_terminates);
  tcase_add_test(tc, nack_threshold_zero_disables_congestion_handling);
  tcase_add_test(tc, nack_from_client_without_burst_is_not_counted);
  tcase_add_test(tc, sdes_and_nack_in_either_order_track_the_cname);
  tcase_add_test(tc, same_cname_from_other_address_is_no_collision_but_no_cname_is);
  tcase_add_test(tc, hned_tracking_is_off_when_rsi_is_inactive);
  tcase_add_test(tc, rams_r_and_rams_t_also_track_hned_addresses);
  tcase_add_test(tc, resolve_callback_attributes_hned_to_the_resolved_channel);
  tcase_add_test(tc, capture_cb_stores_packets_and_provisions_multicast_sockets);
  tcase_add_test(tc, capture_cb_rejects_new_channels_beyond_the_cap);
  tcase_add_test(tc, capture_cb_reaps_idle_channels_step_by_step);
  tcase_add_test(tc, capture_cb_reaps_idle_ret_client_sessions);
  tcase_add_test(tc, capture_cb_feeds_the_inspector_when_enabled);
  tcase_add_test(tc, ret_send_mc_impl_sends_only_when_the_channel_socket_exists);
  tcase_add_test(tc, burst_send_cb_flags_congestion_only_for_would_block_errors);
  tcase_add_test(tc, ipv6_client_is_matched_against_ranges_and_gets_replies);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dispatch_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
