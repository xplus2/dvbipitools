/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <linux/if_packet.h>

#include "dipifccret/capture/capture.h"

typedef struct {
  int called;
  int family;
  char group[64];
  unsigned port;
  unsigned char dscp;
  uint32_t ssrc;
  uint16_t seq;
  uint32_t timestamp;
  unsigned char payload[16];
} record_t;

static void record_cb(int family, const void *addr, size_t addr_len, unsigned port, unsigned char dscp, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, void *user) {
  record_t *r = (record_t *)user;
  (void)addr_len;
  r->called = 1;
  r->family = family;
  if (!inet_ntop(family, addr, r->group, sizeof r->group))
    r->group[0] = '\0';
  r->port = port;
  r->dscp = dscp;
  r->ssrc = ssrc;
  r->seq = seq;
  r->timestamp = timestamp;
  memcpy(r->payload, payload, payload_len < sizeof r->payload ? payload_len : sizeof r->payload);
}

static void make_ranges(cidr_t *ranges) {
  ck_assert_int_eq(cidr_parse("239.1.2.0/24", &ranges[0]), 0);
  ck_assert_int_eq(cidr_parse("ff3e::/32", &ranges[1]), 0);
}

static size_t put_eth(unsigned char *p, int vlan, unsigned ethertype) {
  memset(p, 0xAA, 12); /* dst/src mac, content irrelevant */
  if (vlan) {
    p[12] = 0x81;
    p[13] = 0x00;
    p[14] = 0x00;
    p[15] = 0x01;
    p[16] = (unsigned char)(ethertype >> 8);
    p[17] = (unsigned char)ethertype;
    return 18;
  }
  p[12] = (unsigned char)(ethertype >> 8);
  p[13] = (unsigned char)ethertype;
  return 14;
}

/* RTP header (12B, no CSRC/ext) + a TS-sync-prefixed payload; rtp_valid=0 sets a bogus version to fail rtp_payload_offset */
static size_t put_rtp_ts(unsigned char *p, uint16_t seq, uint32_t timestamp, uint32_t ssrc, int rtp_valid) {
  p[0] = rtp_valid ? 0x80 : 0x00;
  p[1] = 33; /* MP2T payload type */
  p[2] = (unsigned char)(seq >> 8);
  p[3] = (unsigned char)seq;
  p[4] = (unsigned char)(timestamp >> 24);
  p[5] = (unsigned char)(timestamp >> 16);
  p[6] = (unsigned char)(timestamp >> 8);
  p[7] = (unsigned char)timestamp;
  p[8] = (unsigned char)(ssrc >> 24);
  p[9] = (unsigned char)(ssrc >> 16);
  p[10] = (unsigned char)(ssrc >> 8);
  p[11] = (unsigned char)ssrc;
  memset(p + 12, 0xFF, 188);
  p[12] = 0x47; /* TS sync */
  return 12 + 188;
}

static size_t put_ipv4_udp_rtp(unsigned char *p, const char *dst_ip, unsigned dst_port, unsigned char tos, int rtp_valid) {
  size_t udp_off = 20, rtp_off = udp_off + 8;
  size_t rtp_len = put_rtp_ts(p + rtp_off, 1, 0x1000, 0xdeadbeef, rtp_valid);
  struct in_addr dst;

  memset(p, 0, 20);
  p[0] = 0x45; /* version 4, IHL 5 */
  p[1] = tos;
  p[8] = 64;   /* ttl */
  p[9] = 17;   /* udp */
  inet_pton(AF_INET, "192.0.2.1", p + 12);
  inet_pton(AF_INET, dst_ip, &dst);
  memcpy(p + 16, &dst, 4);

  p[udp_off + 0] = 0x13;
  p[udp_off + 1] = 0x88;
  p[udp_off + 2] = (unsigned char)(dst_port >> 8);
  p[udp_off + 3] = (unsigned char)dst_port;
  p[udp_off + 4] = (unsigned char)((8 + rtp_len) >> 8);
  p[udp_off + 5] = (unsigned char)(8 + rtp_len);

  return rtp_off + rtp_len;
}

static size_t put_ipv6_udp_rtp(unsigned char *p, const char *dst_ip, unsigned dst_port, unsigned char tc, int hopbyhop, int rtp_valid) {
  size_t off = 40, udp_off, rtp_off, rtp_len;
  struct in6_addr src, dst;

  memset(p, 0, 40);
  p[0] = (unsigned char)(0x60 | (tc >> 4)); /* version 6, high nibble of Traffic Class */
  p[1] = (unsigned char)(tc << 4); /* low nibble of Traffic Class, flow label 0 */
  p[6] = hopbyhop ? 0 : 17;
  p[7] = 64; /* hop limit */
  inet_pton(AF_INET6, "::1", &src);
  memcpy(p + 8, &src, 16);
  inet_pton(AF_INET6, dst_ip, &dst);
  memcpy(p + 24, &dst, 16);

  if (hopbyhop) {
    p[off + 0] = 17; /* next header: UDP */
    p[off + 1] = 0;  /* (0+1)*8 = 8 bytes total */
    memset(p + off + 2, 0, 6);
    off += 8;
  }

  udp_off = off;
  rtp_off = udp_off + 8;
  rtp_len = put_rtp_ts(p + rtp_off, 1, 0x2000, 0xfeedface, rtp_valid);

  p[udp_off + 0] = 0x13;
  p[udp_off + 1] = 0x89;
  p[udp_off + 2] = (unsigned char)(dst_port >> 8);
  p[udp_off + 3] = (unsigned char)dst_port;
  p[udp_off + 4] = (unsigned char)((8 + rtp_len) >> 8);
  p[udp_off + 5] = (unsigned char)(8 + rtp_len);

  return rtp_off + rtp_len;
}

static size_t build_ipv4_frame(unsigned char *buf, int vlan, const char *dst_ip, unsigned dst_port, unsigned char tos, int rtp_valid) {
  size_t off = put_eth(buf, vlan, 0x0800);
  return off + put_ipv4_udp_rtp(buf + off, dst_ip, dst_port, tos, rtp_valid);
}

static size_t build_ipv6_frame(unsigned char *buf, int vlan, const char *dst_ip, unsigned dst_port, unsigned char tc, int hopbyhop, int rtp_valid) {
  size_t off = put_eth(buf, vlan, 0x86DD);
  return off + put_ipv6_udp_rtp(buf + off, dst_ip, dst_port, tc, hopbyhop, rtp_valid);
}

START_TEST(capture_ipv4_novlan_accepted) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0, 1);
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 1);
  ck_assert_int_eq(rec.family, AF_INET);
  ck_assert_str_eq(rec.group, "239.1.2.5");
  ck_assert_uint_eq(rec.port, 5000);
  ck_assert_uint_eq(rec.ssrc, 0xdeadbeef);
  ck_assert_uint_eq(rec.seq, 1);
  ck_assert_uint_eq(rec.timestamp, 0x1000);
  ck_assert_uint_eq(rec.payload[0], 0x47);
}
END_TEST

START_TEST(capture_ipv4_vlan_tagged_accepted) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv4_frame(pkt, 1, "239.1.2.5", 5000, 0, 1);
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 1);
  ck_assert_str_eq(rec.group, "239.1.2.5");
}
END_TEST

START_TEST(capture_ipv6_hopbyhop_accepted) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv6_frame(pkt, 0, "ff3e::5", 6000, 0, 1, 1);
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 1);
  ck_assert_int_eq(rec.family, AF_INET6);
  ck_assert_str_eq(rec.group, "ff3e::5");
  ck_assert_uint_eq(rec.port, 6000);
}
END_TEST

/* F.9/I.2.12: DSCP extraction feeds RTX/burst DSCP mirroring, top 6 bits of TOS byte */
START_TEST(capture_ipv4_dscp_extracted) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0x88, 1); /* 0b100010 << 2 */
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 1);
  ck_assert_uint_eq(rec.dscp, 0x88);
}
END_TEST

/* ECN bits (low 2 bits of TOS) must not leak into extracted DSCP */
START_TEST(capture_ipv4_dscp_masks_ecn_bits) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0x8B, 1); /* DSCP 0x88 + ECN 0b11 */
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 1);
  ck_assert_uint_eq(rec.dscp, 0x88);
}
END_TEST

START_TEST(capture_ipv6_traffic_class_extracted) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv6_frame(pkt, 0, "ff3e::5", 6000, 0x90, 0, 1); /* 0b100100 << 2 */
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 1);
  ck_assert_uint_eq(rec.dscp, 0x90);
}
END_TEST

START_TEST(capture_out_of_range_rejected) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv4_frame(pkt, 0, "10.0.0.5", 5000, 0, 1);
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 0);
}
END_TEST

START_TEST(capture_non_rtp_payload_rejected) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;
  size_t len;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  len = build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0, 0);
  capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);

  ck_assert_int_eq(rec.called, 0);
}
END_TEST

START_TEST(capture_truncated_frame_rejected) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  record_t rec;

  make_ranges(ranges);
  memset(&rec, 0, sizeof rec);
  build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0, 1);
  capture_handle_frame(pkt, 10, ranges, 2, record_cb, &rec); /* shorter than an ethernet header */

  ck_assert_int_eq(rec.called, 0);
}
END_TEST

#define RING_BLOCK_SIZE 8192u
#define RING_BLOCKS 2u
#define RING_MAX_SEEN 16

typedef struct {
  unsigned ports[RING_MAX_SEEN];
  int n;
} ring_seen_t;

static void ring_cb(int family, const void *addr, size_t addr_len, unsigned port, unsigned char dscp, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, void *user) {
  ring_seen_t *r = user;

  (void)family;
  (void)addr;
  (void)addr_len;
  (void)dscp;
  (void)ssrc;
  (void)seq;
  (void)timestamp;
  (void)payload;
  (void)payload_len;
  if (r->n < RING_MAX_SEEN)
    r->ports[r->n] = port;
  r->n++;
}

static struct tpacket_block_desc *ring_block(unsigned char *ring, unsigned idx) {
  return (struct tpacket_block_desc *)(ring + (size_t)idx * RING_BLOCK_SIZE);
}

static void ring_fill_block(unsigned char *ring, unsigned idx, const char *const *dst_ips, const unsigned *ports, unsigned count) {
  struct tpacket_block_desc *bd = ring_block(ring, idx);
  unsigned char *at;
  unsigned i;

  memset(bd, 0, RING_BLOCK_SIZE);
  bd->hdr.bh1.offset_to_first_pkt = TPACKET_ALIGN(sizeof(struct tpacket_block_desc));
  bd->hdr.bh1.num_pkts = count;
  at = (unsigned char *)bd + bd->hdr.bh1.offset_to_first_pkt;
  for (i = 0; i < count; i++) {
    struct tpacket3_hdr *ppd = (struct tpacket3_hdr *)at;
    unsigned char frame[512];
    size_t len = build_ipv4_frame(frame, 0, dst_ips[i], ports[i], 0, 1);

    ppd->tp_mac = (unsigned short)TPACKET_ALIGN(sizeof(struct tpacket3_hdr));
    ppd->tp_snaplen = (unsigned)len;
    ppd->tp_len = (unsigned)len;
    ppd->tp_next_offset = (unsigned)TPACKET_ALIGN(ppd->tp_mac + len);
    memcpy(at + ppd->tp_mac, frame, len);
    at += ppd->tp_next_offset;
  }
  bd->hdr.bh1.block_status = TP_STATUS_USER;
}

START_TEST(ring_drain_delivers_in_range_frames_and_returns_the_block) {
  unsigned char ring[RING_BLOCK_SIZE * RING_BLOCKS];
  const char *ips[3] = {"239.1.2.5", "10.9.9.9", "239.1.2.6"};
  unsigned ports[3] = {5001, 5002, 5003};
  cidr_t ranges[2];
  ring_seen_t seen = {{0}, 0};
  capture_t *cap;

  make_ranges(ranges);
  ring_fill_block(ring, 0, ips, ports, 3);
  ring_fill_block(ring, 1, ips, ports, 0);
  ring_block(ring, 1)->hdr.bh1.block_status = TP_STATUS_KERNEL;
  cap = capture_from_ring(ring, RING_BLOCK_SIZE, RING_BLOCKS, ranges, 2);
  ck_assert_ptr_nonnull(cap);
  capture_drain_ring(cap, ring_cb, &seen);
  ck_assert_int_eq(seen.n, 2);
  ck_assert_uint_eq(seen.ports[0], 5001u);
  ck_assert_uint_eq(seen.ports[1], 5003u);
  ck_assert_uint_eq(ring_block(ring, 0)->hdr.bh1.block_status, (unsigned)TP_STATUS_KERNEL);
  capture_close(cap);
}
END_TEST

START_TEST(ring_drain_follows_the_blocks_round_robin_and_stops_at_a_kernel_block) {
  unsigned char ring[RING_BLOCK_SIZE * RING_BLOCKS];
  const char *ips[1] = {"239.1.2.5"};
  unsigned p1[1] = {6001};
  unsigned p2[1] = {6002};
  unsigned p3[1] = {6003};
  cidr_t ranges[2];
  ring_seen_t seen = {{0}, 0};
  capture_t *cap;

  make_ranges(ranges);
  ring_fill_block(ring, 0, ips, p1, 1);
  ring_fill_block(ring, 1, ips, p2, 1);
  cap = capture_from_ring(ring, RING_BLOCK_SIZE, RING_BLOCKS, ranges, 2);
  ck_assert_ptr_nonnull(cap);
  capture_drain_ring(cap, ring_cb, &seen);
  ck_assert_int_eq(seen.n, 2);
  capture_drain_ring(cap, ring_cb, &seen);
  ck_assert_int_eq(seen.n, 2);
  ring_fill_block(ring, 0, ips, p3, 1);
  capture_drain_ring(cap, ring_cb, &seen);
  ck_assert_int_eq(seen.n, 3);
  ck_assert_uint_eq(seen.ports[2], 6003u);
  capture_close(cap);
}
END_TEST

START_TEST(ring_drain_with_an_empty_released_block_just_returns_it) {
  unsigned char ring[RING_BLOCK_SIZE * RING_BLOCKS];
  const char *ips[1] = {"239.1.2.5"};
  unsigned ports[1] = {7001};
  cidr_t ranges[2];
  ring_seen_t seen = {{0}, 0};
  capture_t *cap;

  make_ranges(ranges);
  ring_fill_block(ring, 0, ips, ports, 0);
  ring_fill_block(ring, 1, ips, ports, 0);
  ring_block(ring, 1)->hdr.bh1.block_status = TP_STATUS_KERNEL;
  cap = capture_from_ring(ring, RING_BLOCK_SIZE, RING_BLOCKS, ranges, 2);
  capture_drain_ring(cap, ring_cb, &seen);
  ck_assert_int_eq(seen.n, 0);
  ck_assert_uint_eq(ring_block(ring, 0)->hdr.bh1.block_status, (unsigned)TP_STATUS_KERNEL);
  capture_close(cap);
}
END_TEST

START_TEST(ring_capture_without_ranges_is_constructible) {
  unsigned char ring[RING_BLOCK_SIZE];
  capture_t *cap = capture_from_ring(ring, RING_BLOCK_SIZE, 1, NULL, 0);

  ck_assert_ptr_nonnull(cap);
  capture_close(cap);
  capture_close(NULL);
}
END_TEST

/* BPF_LD|BPF_ABS: loads a word/halfword from pkt at f->k into *a. 0 ok, -1: out of bounds */
static int bpf_load_abs(const struct sock_filter *f, unsigned size, const unsigned char *pkt, size_t len, uint32_t *a) {
  if (size == BPF_W) {
    if (f->k + 4 > len)
      return -1;
    *a = ((uint32_t)pkt[f->k] << 24) | ((uint32_t)pkt[f->k + 1] << 16) | ((uint32_t)pkt[f->k + 2] << 8) | pkt[f->k + 3];
  } else if (size == BPF_H) {
    if (f->k + 2 > len)
      return -1;
    *a = ((uint32_t)pkt[f->k] << 8) | pkt[f->k + 1];
  } else {
    ck_abort_msg("unsupported BPF load size");
  }
  return 0;
}

/* minimal classic-BPF interpreter covering exactly opcodes capture_build_bpf emits */
static uint32_t bpf_run(const struct sock_filter *insns, size_t n, const unsigned char *pkt, size_t len) {
  size_t pc = 0;
  uint32_t a = 0;

  while (pc < n) {
    const struct sock_filter *f = &insns[pc];
    unsigned code = f->code;

    if ((code & 0x07) == BPF_LD && (code & 0xe0) == BPF_ABS) {
      unsigned size = code & 0x18;
      if (bpf_load_abs(f, size, pkt, len, &a) != 0)
        return 0;
      pc++;
    } else if (code == (BPF_ALU | BPF_AND | BPF_K)) {
      a &= f->k;
      pc++;
    } else if (code == (BPF_JMP | BPF_JEQ | BPF_K)) {
      pc += 1 + (a == f->k ? f->jt : f->jf);
    } else if (code == (BPF_JMP | BPF_JA)) {
      pc += 1 + f->k;
    } else if (code == (BPF_RET | BPF_K)) {
      return f->k;
    } else {
      ck_abort_msg("unsupported BPF opcode");
    }
  }
  ck_abort_msg("BPF program ran off the end");
  return 0;
}

START_TEST(bpf_accepts_in_range_v4_v6_rejects_others) {
  cidr_t ranges[2];
  struct sock_filter *prog;
  size_t prog_len;
  unsigned char pkt[512];
  size_t len;

  make_ranges(ranges);
  prog = capture_build_bpf(ranges, 2, &prog_len);
  ck_assert_ptr_nonnull(prog);

  len = build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0, 1);
  ck_assert_uint_ne(bpf_run(prog, prog_len, pkt, len), 0);

  len = build_ipv4_frame(pkt, 1, "239.1.2.5", 5000, 0, 1);
  ck_assert_uint_ne(bpf_run(prog, prog_len, pkt, len), 0);

  len = build_ipv6_frame(pkt, 0, "ff3e::5", 6000, 0, 0, 1);
  ck_assert_uint_ne(bpf_run(prog, prog_len, pkt, len), 0);

  len = build_ipv4_frame(pkt, 0, "10.0.0.5", 5000, 0, 1);
  ck_assert_uint_eq(bpf_run(prog, prog_len, pkt, len), 0);

  len = build_ipv6_frame(pkt, 0, "ff3e::5", 6000, 0, 1, 1); /* hop-by-hop: filter doesn't walk ext headers, dst still matches at fixed offset */
  ck_assert_uint_ne(bpf_run(prog, prog_len, pkt, len), 0);

  free(prog);
}
END_TEST

START_TEST(bpf_instruction_count_stays_within_kernel_limit) {
  cidr_t ranges[40];
  struct sock_filter *prog;
  size_t prog_len, i;
  char buf[40][32];

  for (i = 0; i < 40; i++) {
    snprintf(buf[i], sizeof buf[i], "10.%zu.0.0/16", i);
    ck_assert_int_eq(cidr_parse(buf[i], &ranges[i]), 0);
  }
  prog = capture_build_bpf(ranges, 40, &prog_len);
  ck_assert_ptr_nonnull(prog);
  ck_assert_uint_lt(prog_len, 4096);
  free(prog);
}
END_TEST

typedef enum {
  MUT_NONE,
  MUT_V6_FRAGMENT,
  MUT_V6_FRAGMENT_TRUNC,
  MUT_V6_AH,
  MUT_V6_ESP,
  MUT_V6_EXT_TRUNC,
  MUT_V6_SHORT_HDR,
  MUT_V6_BAD_VERSION,
  MUT_VLAN_TRUNC,
  MUT_V4_SHORT_UDP,
  MUT_V4_SHORT_IP,
  MUT_V4_BAD_IHL,
  MUT_V4_BAD_VERSION,
  MUT_V4_TCP,
  MUT_ETHERTYPE_ARP,
  MUT_V4_UDP_ONLY_HEADER
} mutation_t;

typedef struct {
  mutation_t mut;
  int v6;
  int expect_called;
} frame_case_t;

START_TEST(frame_rejections_and_accepts_by_malformation) {
  static const frame_case_t cases[] = {
    {MUT_NONE, 0, 1},
    {MUT_V6_FRAGMENT, 1, 1},
    {MUT_V6_FRAGMENT_TRUNC, 1, 0},
    {MUT_V6_AH, 1, 0},
    {MUT_V6_ESP, 1, 0},
    {MUT_V6_EXT_TRUNC, 1, 0},
    {MUT_V6_SHORT_HDR, 1, 0},
    {MUT_V6_BAD_VERSION, 1, 0},
    {MUT_VLAN_TRUNC, 0, 0},
    {MUT_V4_SHORT_UDP, 0, 0},
    {MUT_V4_SHORT_IP, 0, 0},
    {MUT_V4_BAD_IHL, 0, 0},
    {MUT_V4_BAD_VERSION, 0, 0},
    {MUT_V4_TCP, 0, 0},
    {MUT_ETHERTYPE_ARP, 0, 0},
    {MUT_V4_UDP_ONLY_HEADER, 0, 0},
  };
  size_t i;

  for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    unsigned char pkt[512];
    cidr_t ranges[2];
    record_t rec;
    size_t len;

    make_ranges(ranges);
    memset(&rec, 0, sizeof rec);
    if (cases[i].v6)
      len = build_ipv6_frame(pkt, 0, "ff3e::1234", 5000, 0, 1, 1);
    else
      len = build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0, 1);
    switch (cases[i].mut) {
      case MUT_V6_FRAGMENT: pkt[14 + 6] = 44; break;
      case MUT_V6_FRAGMENT_TRUNC: pkt[14 + 6] = 44; len = 14 + 40 + 4; break;
      case MUT_V6_AH: pkt[14 + 6] = 51; break;
      case MUT_V6_ESP: pkt[14 + 6] = 50; break;
      case MUT_V6_EXT_TRUNC: len = 14 + 40 + 1; break;
      case MUT_V6_SHORT_HDR: len = 14 + 39; break;
      case MUT_V6_BAD_VERSION: pkt[14] = 0x40; break;
      case MUT_VLAN_TRUNC: pkt[12] = 0x81; pkt[13] = 0x00; len = 16; break;
      case MUT_V4_SHORT_UDP: len = 14 + 20 + 4; break;
      case MUT_V4_SHORT_IP: len = 14 + 10; break;
      case MUT_V4_BAD_IHL: pkt[14] = 0x44; break;
      case MUT_V4_BAD_VERSION: pkt[14] = 0x65; break;
      case MUT_V4_TCP: pkt[14 + 9] = 6; break;
      case MUT_ETHERTYPE_ARP: pkt[12] = 0x08; pkt[13] = 0x06; break;
      case MUT_V4_UDP_ONLY_HEADER: len = 14 + 20 + 8; break;
      case MUT_NONE: break;
    }
    capture_handle_frame(pkt, len, ranges, 2, record_cb, &rec);
    ck_assert_msg(rec.called == cases[i].expect_called, "case %zu", i);
  }
}
END_TEST

START_TEST(frame_with_null_callback_is_accepted_silently) {
  unsigned char pkt[512];
  cidr_t ranges[2];
  size_t len;

  make_ranges(ranges);
  len = build_ipv4_frame(pkt, 0, "239.1.2.5", 5000, 0, 1);
  capture_handle_frame(pkt, len, ranges, 2, NULL, NULL);
}
END_TEST

START_TEST(bpf_rejects_range_sets_over_the_kernel_instruction_limit) {
  static cidr_t v4[511];
  static cidr_t v6[160];
  struct sock_filter *prog;
  size_t prog_len;
  size_t i;
  char buf[48];

  for (i = 0; i < 511; i++) {
    snprintf(buf, sizeof buf, "10.%zu.%zu.0/24", i / 256, i % 256);
    ck_assert_int_eq(cidr_parse(buf, &v4[i]), 0);
  }
  prog = capture_build_bpf(v4, 510, &prog_len);
  ck_assert_ptr_nonnull(prog);
  ck_assert_uint_le(prog_len, 4096u);
  free(prog);
  ck_assert_ptr_null(capture_build_bpf(v4, 511, &prog_len));

  for (i = 0; i < 160; i++) {
    snprintf(buf, sizeof buf, "ff3e:%zx::/32", i + 1);
    ck_assert_int_eq(cidr_parse(buf, &v6[i]), 0);
  }
  ck_assert_ptr_null(capture_build_bpf(v6, 160, &prog_len));
}
END_TEST

START_TEST(drop_privileges_fails_for_an_unknown_user_and_ignores_null) {
  ck_assert_int_eq(capture_drop_privileges(NULL), 0);
  ck_assert_int_eq(capture_drop_privileges("no-such-user-dvbipitools-test"), -1);
}
END_TEST

static Suite *capture_suite(void) {
  Suite *s = suite_create("capture");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, capture_ipv4_novlan_accepted);
  tcase_add_test(tc, capture_ipv4_vlan_tagged_accepted);
  tcase_add_test(tc, capture_ipv6_hopbyhop_accepted);
  tcase_add_test(tc, capture_ipv4_dscp_extracted);
  tcase_add_test(tc, capture_ipv4_dscp_masks_ecn_bits);
  tcase_add_test(tc, capture_ipv6_traffic_class_extracted);
  tcase_add_test(tc, capture_out_of_range_rejected);
  tcase_add_test(tc, capture_non_rtp_payload_rejected);
  tcase_add_test(tc, capture_truncated_frame_rejected);
  tcase_add_test(tc, bpf_accepts_in_range_v4_v6_rejects_others);
  tcase_add_test(tc, bpf_instruction_count_stays_within_kernel_limit);
  tcase_add_test(tc, ring_drain_delivers_in_range_frames_and_returns_the_block);
  tcase_add_test(tc, ring_drain_follows_the_blocks_round_robin_and_stops_at_a_kernel_block);
  tcase_add_test(tc, ring_drain_with_an_empty_released_block_just_returns_it);
  tcase_add_test(tc, ring_capture_without_ranges_is_constructible);
  tcase_add_test(tc, frame_rejections_and_accepts_by_malformation);
  tcase_add_test(tc, frame_with_null_callback_is_accepted_silently);
  tcase_add_test(tc, bpf_rejects_range_sets_over_the_kernel_instruction_limit);
  tcase_add_test(tc, drop_privileges_fails_for_an_unknown_user_and_ignores_null);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(capture_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
