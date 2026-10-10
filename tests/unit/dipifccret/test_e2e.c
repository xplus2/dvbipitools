/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipifccret/capture/capture.h"
#include "dipifccret/run/run.h"
#include "lib/demux/crc32.h"
#include "lib/demux/rtx.h"
#include "lib/mux/psi_build.h"
#include "lib/mux/rtcp_build.h"
#include "lib/sys/signal.h"

#define RTX_PT 99
#define MEDIA_SSRC 0xABCD0001u
#define HNED_SSRC 0x11110001u
#define PRE_RAP_SEQ 0
#define RAP_SEQ 1
#define LAST_SEQ 12
#define TS_LEN 188
#define DISCOVERY_LEN (3 * TS_LEN)
#define MAX_REPLIES 32
#define RECV_TIMEOUT_MS 3000

typedef struct {
  channel_table_t *ch;
  ret_ctx_t *ret;
  burst_table_t *bursts;
  dispatch_ctx_t d;
  ret_send_ctx_t rsc;
  cidr_t range;
  int srv;
  int cli;
  struct sockaddr_in cli_addr;
} e2e_t;

typedef struct {
  uint16_t osn;
  size_t len;
  unsigned char data[DISCOVERY_LEN];
} rtx_reply_t;

typedef struct {
  rtx_reply_t rtx[MAX_REPLIES];
  int n_rtx;
  uint16_t rams_i_response[MAX_REPLIES];
  int n_rams_i;
  int has_first_seq;
  uint16_t first_seq;
} replies_t;

static void mc_noop(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)c;
  (void)pkt;
  (void)len;
  (void)dscp;
  (void)user;
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

static void e2e_open(e2e_t *fx, int with_ret, int with_bursts) {
  struct sockaddr_in srv_addr;

  memset(fx, 0, sizeof *fx);
  ck_assert_int_eq(cidr_parse("239.1.1.0/24", &fx->range), 0);
  fx->ch = channel_table_new(8, 64, 64);
  ck_assert_ptr_nonnull(fx->ch);
  fx->d.channels = fx->ch;
  fx->d.rtx_pt = RTX_PT;
  fx->d.burst_multiplier = 1.5;
  fx->d.duration_cap_ms = 2000;
  fx->rsc.last_dscp = -1;
  if (with_ret) {
    fx->ret = ret_ctx_new(fx->ch, RTX_PT, 8, mc_noop, ret_send_unicast_impl, &fx->rsc);
    ck_assert_ptr_nonnull(fx->ret);
    fx->d.ret = fx->ret;
  }
  if (with_bursts) {
    fx->bursts = burst_table_new(2);
    ck_assert_ptr_nonnull(fx->bursts);
    fx->d.bursts = fx->bursts;
  }
  fx->srv = udp_bound(&srv_addr);
  fx->cli = udp_bound(&fx->cli_addr);
}

static void e2e_close(e2e_t *fx) {
  close(fx->srv);
  close(fx->cli);
  ret_ctx_free(fx->ret);
  burst_table_free(fx->bursts);
  channel_table_free(fx->ch);
}

static void wrap_section_packet(unsigned char pkt[TS_LEN], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  memset(pkt + 5 + slen, 0xFF, TS_LEN - 5 - slen);
}

static size_t build_pmt_section(unsigned char *sec, unsigned prog_num, unsigned video_pid) {
  unsigned char body[16];
  size_t n = 0, crc_at, hdr;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((video_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)video_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x1B; /* H264 */
  body[n++] = (unsigned char)(0xE0 | ((video_pid >> 8) & 0x1F));
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
  return crc_at + 4;
}

/* PAT + PMT + video packet carrying random_access_indicator */
static void build_discovery(unsigned char out[DISCOVERY_LEN]) {
  unsigned char sec[64];
  size_t slen;

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(out, 0x0000, sec, slen);
  slen = build_pmt_section(sec, 101, 0x0101);
  wrap_section_packet(out + TS_LEN, 0x0100, sec, slen);
  memset(out + 2 * TS_LEN, 0xCD, TS_LEN);
  out[2 * TS_LEN + 0] = 0x47;
  out[2 * TS_LEN + 1] = 0x01;
  out[2 * TS_LEN + 2] = 0x01;
  out[2 * TS_LEN + 3] = 0x30;
  out[2 * TS_LEN + 4] = 0x01;
  out[2 * TS_LEN + 5] = 0x40;
}

static void build_plain_ts(unsigned char out[TS_LEN], uint16_t seq) {
  memset(out, 0x20 + seq, TS_LEN);
  out[0] = 0x47;
  out[1] = 0x01;
  out[2] = 0x01;
  out[3] = 0x10;
}

static size_t build_payload(unsigned char *out, uint16_t seq) {
  if (seq == RAP_SEQ) {
    build_discovery(out);
    return DISCOVERY_LEN;
  }
  build_plain_ts(out, seq);
  return TS_LEN;
}

/* Ethernet + IPv4 + UDP + RTP(PT 33) around payload, then pad trailer bytes the way a NIC pads short frames */
static size_t build_frame(unsigned char *p, uint16_t seq, const unsigned char *payload, size_t payload_len, size_t pad) {
  size_t ip = 14, udp = ip + 20, rtp = udp + 8, total;
  uint32_t ts = (uint32_t)seq * 3600u;

  memset(p, 0xAA, 12);
  p[12] = 0x08;
  p[13] = 0x00;
  memset(p + ip, 0, 20);
  p[ip + 0] = 0x45;
  p[ip + 8] = 64;
  p[ip + 9] = 17;
  inet_pton(AF_INET, "192.0.2.1", p + ip + 12);
  inet_pton(AF_INET, "239.1.1.1", p + ip + 16);
  p[udp + 0] = 0x13;
  p[udp + 1] = 0x88;
  p[udp + 2] = 0x13;
  p[udp + 3] = 0x88;
  p[udp + 4] = (unsigned char)((8 + 12 + payload_len) >> 8);
  p[udp + 5] = (unsigned char)(8 + 12 + payload_len);
  p[rtp + 0] = 0x80;
  p[rtp + 1] = 33;
  p[rtp + 2] = (unsigned char)(seq >> 8);
  p[rtp + 3] = (unsigned char)seq;
  p[rtp + 4] = (unsigned char)(ts >> 24);
  p[rtp + 5] = (unsigned char)(ts >> 16);
  p[rtp + 6] = (unsigned char)(ts >> 8);
  p[rtp + 7] = (unsigned char)ts;
  p[rtp + 8] = (unsigned char)(MEDIA_SSRC >> 24);
  p[rtp + 9] = (unsigned char)(MEDIA_SSRC >> 16);
  p[rtp + 10] = (unsigned char)(MEDIA_SSRC >> 8);
  p[rtp + 11] = (unsigned char)MEDIA_SSRC;
  memcpy(p + rtp + 12, payload, payload_len);
  total = rtp + 12 + payload_len;
  memset(p + total, 0, pad);
  return total + pad;
}

/* every frame from PRE_RAP_SEQ to LAST_SEQ, even seqs carry link-layer padding */
static void feed_stream(e2e_t *fx) {
  for (uint16_t seq = PRE_RAP_SEQ; seq <= LAST_SEQ; seq++) {
    unsigned char payload[DISCOVERY_LEN];
    unsigned char frame[2048];
    size_t plen = build_payload(payload, seq);
    size_t flen = build_frame(frame, seq, payload, plen, (seq % 2 == 0) ? 6 : 0);

    capture_handle_frame(frame, flen, &fx->range, 1, capture_cb, &fx->d);
  }
}

static void send_to_server(e2e_t *fx, const unsigned char *pkt, size_t len) {
  listen_cb(pkt, len, fx->srv, (const struct sockaddr *)&fx->cli_addr, sizeof fx->cli_addr, &fx->d);
}

static void rams_i_collect(const rtcp_rams_i_t *info, void *user) {
  replies_t *r = user;

  if (r->n_rams_i < MAX_REPLIES)
    r->rams_i_response[r->n_rams_i] = info->response;
  r->n_rams_i++;
  if (info->has_first_packet_seqnum) {
    r->has_first_seq = 1;
    r->first_seq = info->first_packet_seqnum;
  }
}

/* read one datagram from the client socket into r. 0 on timeout */
static int recv_one(const e2e_t *fx, replies_t *r, int timeout_ms) {
  struct pollfd pfd = {.fd = fx->cli, .events = POLLIN};
  unsigned char buf[2048];
  ssize_t n;

  if (poll(&pfd, 1, timeout_ms) <= 0) return 0;
  n = recv(fx->cli, buf, sizeof buf, 0);
  if (n <= 0) return 0;
  if (buf[1] >= 192 && buf[1] <= 223) {
    rtcp_parse(buf, (size_t)n, &(rtcp_cbs_t){.rams_i_cb = rams_i_collect, .user = r});
  } else {
    rtx_pkt_t rx;

    ck_assert(rtx_parse(buf, (size_t)n, RTX_PT, &rx));
    ck_assert_uint_le(rx.payload_len, sizeof r->rtx[0].data);
    if (r->n_rtx < MAX_REPLIES) {
      r->rtx[r->n_rtx].osn = rx.osn;
      r->rtx[r->n_rtx].len = rx.payload_len;
      memcpy(r->rtx[r->n_rtx].data, rx.payload, rx.payload_len);
    }
    r->n_rtx++;
  }
  return 1;
}

/* collect until n_rtx datagrams of media arrived or the wait ran dry */
static void drain_media(const e2e_t *fx, replies_t *r, int want_rtx) {
  while (r->n_rtx < want_rtx && recv_one(fx, r, RECV_TIMEOUT_MS)) {
  }
}

static void expect_original(const rtx_reply_t *got, uint16_t seq) {
  unsigned char want[DISCOVERY_LEN];
  size_t wlen = build_payload(want, seq);

  ck_assert_uint_eq(got->osn, seq);
  ck_assert_uint_eq(got->len, wlen);
  ck_assert_int_eq(memcmp(got->data, want, wlen), 0);
}

START_TEST(captured_frames_fill_ret_ring_and_fcc_cache_without_link_padding) {
  e2e_t fx;
  channel_t *c;
  unsigned char addr[4];
  rap_cache_entry_t e;

  e2e_open(&fx, 1, 1);
  feed_stream(&fx);
  inet_pton(AF_INET, "239.1.1.1", addr);
  c = channel_lookup(fx.ch, AF_INET, addr, sizeof addr, 5000);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(channel_has_rap(c), 1);
  ck_assert_uint_eq(channel_cache_count(c), (size_t)(LAST_SEQ - RAP_SEQ + 1));
  ck_assert_int_eq(channel_cache_get(c, 0, &e), 1);
  ck_assert_uint_eq(e.seq, RAP_SEQ);
  ck_assert_uint_eq(e.payload_len, DISCOVERY_LEN);
  ck_assert_int_eq(channel_cache_get(c, 1, &e), 1);
  ck_assert_uint_eq(e.seq, RAP_SEQ + 1); /* seq 2 arrived with padding */
  ck_assert_uint_eq(e.payload_len, TS_LEN);
  e2e_close(&fx);
}
END_TEST

START_TEST(nack_for_captured_packets_returns_the_original_payloads) {
  e2e_t fx;
  unsigned char pkt[128];
  rtcp_nack_entry_t entry = {2, 0x0005}; /* seq 2, 3, 5 */
  replies_t r;
  size_t n;

  e2e_open(&fx, 1, 0);
  feed_stream(&fx);
  n = rtcp_build_ff(HNED_SSRC, MEDIA_SSRC, &entry, 1, pkt, sizeof pkt);
  ck_assert_uint_gt(n, 0u);
  send_to_server(&fx, pkt, n);

  memset(&r, 0, sizeof r);
  drain_media(&fx, &r, 3);
  ck_assert_int_eq(r.n_rtx, 3);
  expect_original(&r.rtx[0], 2); /* even seq, was padded on the wire */
  expect_original(&r.rtx[1], 3);
  expect_original(&r.rtx[2], 5);
  e2e_close(&fx);
}
END_TEST

START_TEST(nack_for_the_rap_packet_returns_the_whole_discovery_payload) {
  e2e_t fx;
  unsigned char pkt[128];
  rtcp_nack_entry_t entry = {RAP_SEQ, 0};
  replies_t r;
  size_t n;

  e2e_open(&fx, 1, 0);
  feed_stream(&fx);
  n = rtcp_build_ff(HNED_SSRC, MEDIA_SSRC, &entry, 1, pkt, sizeof pkt);
  send_to_server(&fx, pkt, n);

  memset(&r, 0, sizeof r);
  drain_media(&fx, &r, 1);
  ck_assert_int_eq(r.n_rtx, 1);
  expect_original(&r.rtx[0], RAP_SEQ);
  e2e_close(&fx);
}
END_TEST

START_TEST(rams_r_accepts_and_the_pacer_bursts_the_cache_from_the_rap) {
  e2e_t fx;
  unsigned char pkt[128];
  rtcp_rams_r_t req;
  replies_t r;
  pthread_t th;
  pacer_ctx_t pc;
  channel_t *c;
  unsigned char addr[4];
  size_t n;
  int done = 0;
  int guard;

  e2e_open(&fx, 0, 1);
  feed_stream(&fx);
  inet_pton(AF_INET, "239.1.1.1", addr);
  c = channel_lookup(fx.ch, AF_INET, addr, sizeof addr, 5000);
  ck_assert_ptr_nonnull(c);
  atomic_store(&c->nominal_bps, 8000000.0); /* estimator needs wall-clock seconds, pin it */

  memset(&req, 0, sizeof req);
  req.sender_ssrc = HNED_SSRC;
  req.media_ssrc = MEDIA_SSRC;
  n = rtcp_build_rams_r(&req, pkt, sizeof pkt);
  ck_assert_uint_gt(n, 0u);
  send_to_server(&fx, pkt, n);

  memset(&r, 0, sizeof r);
  ck_assert_int_eq(recv_one(&fx, &r, RECV_TIMEOUT_MS), 1);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i_response[0], (unsigned)BURST_ACCEPT);
  ck_assert_int_eq(r.has_first_seq, 1);
  ck_assert_uint_eq(r.first_seq, RAP_SEQ);

  pc = (pacer_ctx_t){.bursts = fx.bursts, .duration_cap_ms = fx.d.duration_cap_ms};
  ck_assert_int_eq(pthread_create(&th, NULL, pacer_main, &pc), 0);
  for (guard = 0; guard < 4 * MAX_REPLIES && !done; guard++) {
    if (!recv_one(&fx, &r, RECV_TIMEOUT_MS)) break;
    done = r.n_rams_i >= 2;
  }
  signals_install();
  raise(SIGTERM);
  pthread_join(th, NULL);

  ck_assert_int_eq(done, 1);
  ck_assert_uint_eq(r.rams_i_response[1], (unsigned)BURST_DONE);
  ck_assert_int_eq(r.n_rtx, LAST_SEQ - RAP_SEQ + 1);
  for (int i = 0; i < r.n_rtx; i++)
    expect_original(&r.rtx[i], (uint16_t)(RAP_SEQ + i));
  e2e_close(&fx);
}
END_TEST

START_TEST(rams_r_before_any_rap_is_refused) {
  e2e_t fx;
  unsigned char pkt[128];
  unsigned char payload[TS_LEN];
  unsigned char frame[2048];
  rtcp_rams_r_t req;
  replies_t r;
  size_t n;

  e2e_open(&fx, 0, 1);
  build_plain_ts(payload, PRE_RAP_SEQ);
  n = build_frame(frame, PRE_RAP_SEQ, payload, sizeof payload, 0);
  capture_handle_frame(frame, n, &fx.range, 1, capture_cb, &fx.d);

  memset(&req, 0, sizeof req);
  req.sender_ssrc = HNED_SSRC;
  req.media_ssrc = MEDIA_SSRC;
  n = rtcp_build_rams_r(&req, pkt, sizeof pkt);
  send_to_server(&fx, pkt, n);

  memset(&r, 0, sizeof r);
  ck_assert_int_eq(recv_one(&fx, &r, RECV_TIMEOUT_MS), 1);
  ck_assert_int_eq(r.n_rams_i, 1);
  ck_assert_uint_eq(r.rams_i_response[0], (unsigned)BURST_NO_RAP);
  ck_assert_int_eq(r.n_rtx, 0);
  e2e_close(&fx);
}
END_TEST

static Suite *e2e_suite(void) {
  Suite *s = suite_create("dipifccret_e2e");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 20);
  tcase_add_test(tc, captured_frames_fill_ret_ring_and_fcc_cache_without_link_padding);
  tcase_add_test(tc, nack_for_captured_packets_returns_the_original_payloads);
  tcase_add_test(tc, nack_for_the_rap_packet_returns_the_whole_discovery_payload);
  tcase_add_test(tc, rams_r_accepts_and_the_pacer_bursts_the_cache_from_the_rap);
  tcase_add_test(tc, rams_r_before_any_rap_is_refused);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(e2e_suite());
  int failed;

  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
