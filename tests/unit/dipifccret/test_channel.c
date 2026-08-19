/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipifccret/channel/channel.h"
#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"

/* channel_lookup() takes raw address bytes, this converts IPv4 literals for tests */
static channel_t *lookup_ip(channel_table_t *t, const char *ip, unsigned port) {
  unsigned char addr[4];
  inet_pton(AF_INET, ip, addr);
  return channel_lookup(t, AF_INET, addr, sizeof addr, port);
}

static void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
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
  a = lookup_ip(t, "239.1.1.1", 5000);
  ck_assert_ptr_nonnull(a);
  b = lookup_ip(t, "239.1.1.2", 5000);
  ck_assert_ptr_nonnull(b);
  ck_assert_ptr_ne(a, b);
  a_again = lookup_ip(t, "239.1.1.1", 5000);
  ck_assert_ptr_eq(a, a_again);
  c = lookup_ip(t, "239.1.1.3", 5000); /* table only has 2 slots */
  ck_assert_ptr_null(c);

  channel_table_free(t);
}
END_TEST

/* group/addr/addr_len populated on claim. mcsend.c's mcast_open_send() needs c->group text */
START_TEST(channel_lookup_populates_addr_and_text_group) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *c = lookup_ip(t, "239.5.6.7", 5000);
  unsigned char expect[4];

  ck_assert_ptr_nonnull(c);
  inet_pton(AF_INET, "239.5.6.7", expect);
  ck_assert_int_eq(c->family, AF_INET);
  ck_assert_uint_eq(c->addr_len, sizeof expect);
  ck_assert_mem_eq(c->addr, expect, sizeof expect);
  ck_assert_str_eq(c->group, "239.5.6.7");

  channel_table_free(t);
}
END_TEST

START_TEST(channel_store_and_find_ret_ring_round_trips) {
  channel_table_t *t = channel_table_new(1, 4, 0); /* RET only, 4 slots */
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  unsigned char payload[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  channel_slot_t out;
  channel_store(t, c, 0xAABBCCDD, 100, 900000, 0x88, payload, sizeof payload);
  ck_assert_int_eq(channel_find(c, 100, &out), 1);
  ck_assert_uint_eq(out.seq, 100u);
  ck_assert_uint_eq(out.timestamp, 900000u);
  ck_assert_uint_eq(out.dscp, 0x88u);
  ck_assert_uint_eq(out.payload_len, sizeof payload);
  ck_assert_mem_eq(out.payload, payload, sizeof payload);
  ck_assert_int_eq(out.valid, 1);
  ck_assert_int_eq(channel_find(c, 999, &out), 0); /* never stored */
  channel_table_free(t);
}
END_TEST

START_TEST(channel_find_returns_zero_when_ring_disabled) {
  channel_table_t *t = channel_table_new(1, 0, 0); /* no RET */
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  channel_slot_t out;
  unsigned char payload[4] = {1, 2, 3, 4};
  channel_store(t, c, 1, 1, 1, 0, payload, sizeof payload);
  ck_assert_int_eq(channel_find(c, 1, &out), 0);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_ring_wraps_and_overwrites_oldest) {
  channel_table_t *t = channel_table_new(1, 2, 0); /* only 2 ring slots */
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  unsigned char p1[1] = {1}, p2[1] = {2}, p3[1] = {3};
  channel_slot_t out;
  channel_store(t, c, 1, 10, 0, 0, p1, 1); /* slot 10%2=0 */
  channel_store(t, c, 1, 11, 0, 0, p2, 1); /* slot 11%2=1 */
  channel_store(t, c, 1, 12, 0, 0, p3, 1); /* slot 12%2=0, overwrites seq 10's slot */
  ck_assert_int_eq(channel_find(c, 10, &out), 0); /* overwritten: slot now holds seq 12 */
  ck_assert_int_eq(channel_find(c, 11, &out), 1);
  ck_assert_int_eq(channel_find(c, 12, &out), 1);
  ck_assert_uint_eq(out.payload[0], 3u);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_find_by_ssrc_locates_the_right_channel) {
  channel_table_t *t = channel_table_new(2, 1, 0);
  channel_t *a = lookup_ip(t, "239.1.1.1", 5000);
  channel_t *b = lookup_ip(t, "239.1.1.2", 5000);
  unsigned char payload[1] = {0};
  channel_store(t, a, 0x1111, 1, 0, 0, payload, 1);
  channel_store(t, b, 0x2222, 1, 0, 0, payload, 1);
  ck_assert_ptr_eq(channel_find_by_ssrc(t, 0x1111), a);
  ck_assert_ptr_eq(channel_find_by_ssrc(t, 0x2222), b);
  ck_assert_ptr_null(channel_find_by_ssrc(t, 0x9999));
  channel_table_free(t);
}
END_TEST

START_TEST(channel_lookup_by_resolve_slot_finds_the_right_channel) {
  channel_table_t *t = channel_table_new(4, 0, 0);
  channel_t *a = lookup_ip(t, "239.1.1.1", 5000);
  channel_t *b = lookup_ip(t, "239.1.1.2", 5000);
  ck_assert_ptr_eq(channel_lookup_by_resolve_slot(t, a->resolve_slot), a);
  ck_assert_ptr_eq(channel_lookup_by_resolve_slot(t, b->resolve_slot), b);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_lookup_by_resolve_slot_rejects_out_of_range) {
  channel_table_t *t = channel_table_new(4, 0, 0);
  ck_assert_ptr_null(channel_lookup_by_resolve_slot(t, 4));
  ck_assert_ptr_null(channel_lookup_by_resolve_slot(t, 1000));
  channel_table_free(t);
}
END_TEST

START_TEST(channel_lookup_by_resolve_slot_returns_null_for_unclaimed_slot) {
  channel_table_t *t = channel_table_new(4, 0, 0);
  channel_t *a = lookup_ip(t, "239.1.1.1", 5000);
  size_t unclaimed = (size_t)-1;
  for (size_t i = 0; i < 4; i++) {
    if (i != a->resolve_slot) {
      unclaimed = i;
      break;
    }
  }
  ck_assert_uint_ne(unclaimed, (size_t)-1);
  ck_assert_ptr_null(channel_lookup_by_resolve_slot(t, unclaimed));
  channel_table_free(t);
}
END_TEST

/* same key always hashes to same slot: predictable without dipifccret's discovery order */
START_TEST(channel_lookup_by_resolve_slot_survives_reap_and_rediscovery) {
  channel_table_t *t = channel_table_new(4, 0, 0);
  channel_t *a = lookup_ip(t, "239.1.1.1", 5000);
  size_t slot = a->resolve_slot;

  atomic_store_explicit(&a->last_seen, time(NULL) - 100, memory_order_relaxed);
  channel_table_reap(t, 10);
  ck_assert_ptr_null(channel_lookup_by_resolve_slot(t, slot));

  a = lookup_ip(t, "239.1.1.1", 5000);
  ck_assert_uint_eq(a->resolve_slot, slot);
  ck_assert_ptr_eq(channel_lookup_by_resolve_slot(t, slot), a);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_fcc_cache_tracks_rap_and_entries) {
  channel_table_t *t = channel_table_new(1, 0, 8); /* FCC only, 8-entry cache */
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  unsigned char discovery[3 * 188];
  unsigned char plain[188];
  size_t dlen;

  ck_assert_int_eq(channel_has_rap(c), 0);
  ck_assert_uint_eq(channel_cache_count(c), 0u);

  /* before any RAP: stored but not cached */
  memset(plain, 0xAB, sizeof plain);
  plain[0] = 0x47;
  channel_store(t, c, 1, 1, 0, 0, plain, sizeof plain);
  ck_assert_int_eq(channel_has_rap(c), 0);
  ck_assert_uint_eq(channel_cache_count(c), 0u);
  dlen = build_pat_pmt_rai(discovery, 101, 0x0100, 0x0101);
  channel_store(t, c, 1, 2, 0, 0x90, discovery, dlen); /* PAT+PMT+RAI video in one call */
  ck_assert_int_eq(channel_has_rap(c), 1);
  ck_assert_uint_eq(channel_cache_count(c), 1u); /* RAP entry counts */
  channel_store(t, c, 1, 3, 0, 0, plain, sizeof plain);
  ck_assert_uint_eq(channel_cache_count(c), 2u);
  {
    rap_cache_entry_t e;
    ck_assert_int_eq(channel_cache_get(c, 0, &e), 1);
    ck_assert_uint_eq(e.seq, 2u); /* RAP-bearing store. seq 1 was pre-RAP, never cached */
    ck_assert_uint_eq(e.dscp, 0x90u);
    ck_assert_int_eq(channel_cache_get(c, 1, &e), 1);
    ck_assert_uint_eq(e.seq, 3u);
    ck_assert_int_eq(channel_cache_get(c, 2, &e), 0); /* nothing there yet */
  }
  channel_table_free(t);
}
END_TEST

START_TEST(channel_has_rap_stays_zero_when_fcc_disabled) {
  channel_table_t *t = channel_table_new(1, 0, 0); /* cache_cap 0: FCC off */
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  unsigned char discovery[3 * 188];
  size_t dlen = build_pat_pmt_rai(discovery, 101, 0x0100, 0x0101);
  channel_store(t, c, 1, 1, 0, 0, discovery, dlen);
  ck_assert_int_eq(channel_has_rap(c), 0);
  ck_assert_uint_eq(channel_cache_count(c), 0u);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_table_reap_frees_stale_channels) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *a, *b;
  a = lookup_ip(t, "239.1.1.1", 5000);
  ck_assert_ptr_nonnull(a);
  channel_table_reap(t, -1); /* max_age -1: always "older" than now */
  b = lookup_ip(t, "239.1.1.2", 5000); /* slot should be free again */
  ck_assert_ptr_nonnull(b);
  ck_assert_ptr_eq(a, b); /* same underlying slot, reused */
  channel_table_free(t);
}
END_TEST

/* F.3.2.1: rtx_seq_mc is MC RET session's own RTX seq space, one per channel;
   reclaimed slot must start that space fresh for new group */
START_TEST(channel_lookup_reclaim_resets_rtx_seq_mc) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *a, *b;
  a = lookup_ip(t, "239.1.1.1", 5000);
  ck_assert_ptr_nonnull(a);
  atomic_store_explicit(&a->rtx_seq_mc, 42, memory_order_relaxed);
  channel_table_reap(t, -1); /* max_age -1: always "older" than now */
  b = lookup_ip(t, "239.1.1.2", 5000); /* slot reused for a different group */
  ck_assert_ptr_eq(a, b);
  ck_assert_uint_eq(atomic_load_explicit(&b->rtx_seq_mc, memory_order_relaxed), 0u);
  channel_table_free(t);
}
END_TEST

/* max_scan bounds work per call: full table frees slots only as cursor visits them,
   never all at once. cursor advances every call, eventually covers whole table */
START_TEST(channel_table_reap_step_bounds_work_per_call) {
  channel_table_t *t = channel_table_new(4, 0, 0);
  channel_t *a, *e;

  a = lookup_ip(t, "239.2.2.1", 5000);
  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_nonnull(lookup_ip(t, "239.2.2.2", 5000));
  ck_assert_ptr_nonnull(lookup_ip(t, "239.2.2.3", 5000));
  ck_assert_ptr_nonnull(lookup_ip(t, "239.2.2.4", 5000));
  ck_assert_ptr_null(lookup_ip(t, "239.2.2.5", 5000)); /* table full */

  channel_table_reap_step(t, -1, 1); /* max_age -1: always stale. scans 1 slot */
  e = lookup_ip(t, "239.2.2.5", 5000);
  ck_assert_ptr_nonnull(e);
  ck_assert_ptr_eq(e, a); /* reused only slot cursor visited */

  ck_assert_ptr_null(lookup_ip(t, "239.2.2.6", 5000)); /* full again: cursor hasn't reached rest yet */

  channel_table_reap_step(t, -1, 1);
  channel_table_reap_step(t, -1, 1);
  channel_table_reap_step(t, -1, 1);
  ck_assert_ptr_nonnull(lookup_ip(t, "239.2.2.7", 5000));
  ck_assert_ptr_nonnull(lookup_ip(t, "239.2.2.8", 5000));

  channel_table_free(t);
}
END_TEST

START_TEST(channel_lookup_churn_triggers_rebuild_without_hanging) {
  channel_table_t *t = channel_table_new(2, 0, 0); /* hash_size 4: tombstones cross 75% fast */

  for (int i = 0; i < 20; i++) {
    char group[32];
    channel_t *c;
    snprintf(group, sizeof group, "239.9.9.%d", i + 1);
    c = lookup_ip(t, group, 5000);
    ck_assert_ptr_nonnull(c);
    channel_table_reap(t, -1); /* expire immediately: piles up tombstones */
  }

  {
    channel_t *fresh = lookup_ip(t, "239.9.9.200", 5000);
    ck_assert_ptr_nonnull(fresh);
    ck_assert_ptr_eq(lookup_ip(t, "239.9.9.200", 5000), fresh);
  }
  channel_table_free(t);
}
END_TEST

/* race tests: concurrent pthreads on seqlock/CAS paths, TSan-checked */

#define RET_RACE_SEQS 2000
#define RET_RACE_READER_THREADS 4
#define RET_RACE_READER_ITERS 20000

static channel_table_t *g_ret_race_table;
static channel_t *g_ret_race_chan;
static _Atomic int g_ret_race_bad;

static void *ret_race_writer(void *arg) {
  (void)arg;
  for (uint16_t seq = 0; seq < RET_RACE_SEQS; seq++) {
    unsigned char payload[16];
    memset(payload, 0, sizeof payload);
    payload[0] = (unsigned char)(seq >> 8);
    payload[1] = (unsigned char)seq;
    payload[2] = (unsigned char)(seq >> 8);
    payload[3] = (unsigned char)seq;
    channel_store(g_ret_race_table, g_ret_race_chan, 0xAAAAAAAAu, seq, (uint32_t)seq * 90000u, 0, payload, sizeof payload);
  }
  return NULL;
}

static void *ret_race_reader(void *arg) {
  for (int i = 0; i < RET_RACE_READER_ITERS; i++) {
    long idx = (long)arg;
    uint16_t seq = (uint16_t)((i * 7 + idx * 13) % RET_RACE_SEQS);
    channel_slot_t out;
    if (channel_find(g_ret_race_chan, seq, &out)) {
      if (out.payload[0] != (unsigned char)(out.seq >> 8) ||
          out.payload[1] != (unsigned char)out.seq ||
          out.payload[2] != (unsigned char)(out.seq >> 8) ||
          out.payload[3] != (unsigned char)out.seq ||
          out.timestamp != (uint32_t)out.seq * 90000u)
        atomic_store_explicit(&g_ret_race_bad, 1, memory_order_relaxed);
    }
  }
  return NULL;
}

START_TEST(channel_seqlock_race_no_torn_reads) {
  channel_table_t *t = channel_table_new(1, 64, 0); /* small ring: wraps under writer */
  pthread_t writer, readers[RET_RACE_READER_THREADS];
  long i;

  g_ret_race_table = t;
  g_ret_race_chan = lookup_ip(t, "239.7.7.7", 5000);
  atomic_store_explicit(&g_ret_race_bad, 0, memory_order_relaxed);

  pthread_create(&writer, NULL, ret_race_writer, NULL);
  for (i = 0; i < RET_RACE_READER_THREADS; i++)
    pthread_create(&readers[i], NULL, ret_race_reader, (void *)i);

  pthread_join(writer, NULL);
  for (i = 0; i < RET_RACE_READER_THREADS; i++)
    pthread_join(readers[i], NULL);

  ck_assert_int_eq(atomic_load_explicit(&g_ret_race_bad, memory_order_relaxed), 0);
  channel_table_free(t);
}
END_TEST

#define FCC_RACE_APPENDS 2000
#define FCC_RACE_READER_THREADS 4
#define FCC_RACE_READER_ITERS 20000

static channel_table_t *g_fcc_race_table;
static channel_t *g_fcc_race_chan;
static _Atomic int g_fcc_race_bad;

static void *fcc_race_writer(void *arg) {
  (void)arg;
  for (uint16_t seq = 1; seq <= FCC_RACE_APPENDS; seq++) {
    unsigned char payload[16];
    memset(payload, 0, sizeof payload);
    payload[0] = (unsigned char)(seq >> 8);
    payload[1] = (unsigned char)seq;
    payload[2] = (unsigned char)(seq >> 8);
    payload[3] = (unsigned char)seq;
    channel_store(g_fcc_race_table, g_fcc_race_chan, 0xBBBBBBBBu, seq, (uint32_t)seq * 90000u, 0, payload, sizeof payload);
  }
  return NULL;
}

static void *fcc_race_reader(void *arg) {
  (void)arg;
  for (int i = 0; i < FCC_RACE_READER_ITERS; i++) {
    size_t count = channel_cache_count(g_fcc_race_chan);
    rap_cache_entry_t e;
    if (count == 0)
      continue;
    if (!channel_cache_get(g_fcc_race_chan, (size_t)(i % (int)count), &e))
      continue;
    if (e.seq == 0) /* seed entry, unformatted, skip */
      continue;
    if (e.payload[0] != (unsigned char)(e.seq >> 8) ||
        e.payload[1] != (unsigned char)e.seq ||
        e.payload[2] != (unsigned char)(e.seq >> 8) ||
        e.payload[3] != (unsigned char)e.seq ||
        e.timestamp != (uint32_t)e.seq * 90000u)
      atomic_store_explicit(&g_fcc_race_bad, 1, memory_order_relaxed);
  }
  return NULL;
}

START_TEST(channel_fcc_cache_race_no_torn_reads) {
  channel_table_t *t = channel_table_new(1, 0, 8); /* small cache: wraps under writer */
  unsigned char discovery[3 * 188];
  size_t dlen;
  pthread_t writer, readers[FCC_RACE_READER_THREADS];
  long i;

  g_fcc_race_table = t;
  g_fcc_race_chan = lookup_ip(t, "239.8.8.8", 5000);
  dlen = build_pat_pmt_rai(discovery, 101, 0x0100, 0x0101);
  channel_store(g_fcc_race_table, g_fcc_race_chan, 0xBBBBBBBBu, 0, 0, 0, discovery, dlen); /* seeds have_rap before threads start */
  atomic_store_explicit(&g_fcc_race_bad, 0, memory_order_relaxed);

  pthread_create(&writer, NULL, fcc_race_writer, NULL);
  for (i = 0; i < FCC_RACE_READER_THREADS; i++)
    pthread_create(&readers[i], NULL, fcc_race_reader, NULL);

  pthread_join(writer, NULL);
  for (i = 0; i < FCC_RACE_READER_THREADS; i++)
    pthread_join(readers[i], NULL);

  ck_assert_int_eq(atomic_load_explicit(&g_fcc_race_bad, memory_order_relaxed), 0);
  channel_table_free(t);
}
END_TEST

/* channel_store() takes t->lock whenever ssrc changes, concurrently with
   channel_find_by_ssrc() (capture thread vs RET's NACK listener). exercises
   ssrc changing under writer while readers hash-lookup it. */
#define SSRC_RACE_ITERS 20000
#define SSRC_RACE_READER_THREADS 4
#define SSRC_RACE_READER_ITERS 20000
#define SSRC_RACE_SPAN 8 /* distinct ssrcs cycled per channel */

static channel_table_t *g_ssrc_race_table;
static channel_t *g_ssrc_race_a, *g_ssrc_race_b;
static _Atomic int g_ssrc_race_bad;

static void *ssrc_race_writer(void *arg) {
  (void)arg;
  for (int i = 0; i < SSRC_RACE_ITERS; i++) {
    unsigned char payload[4] = {0};
    channel_store(g_ssrc_race_table, g_ssrc_race_a, 0x10000000u + (uint32_t)(i % SSRC_RACE_SPAN), (uint16_t)i, 0, 0, payload, sizeof payload);
    channel_store(g_ssrc_race_table, g_ssrc_race_b, 0x20000000u + (uint32_t)(i % SSRC_RACE_SPAN), (uint16_t)i, 0, 0, payload, sizeof payload);
  }
  return NULL;
}

static void *ssrc_race_reader(void *arg) {
  long idx = (long)arg;
  int i;
  for (i = 0; i < SSRC_RACE_READER_ITERS; i++) {
    uint32_t base = ((i + idx) % 2) ? 0x10000000u : 0x20000000u;
    uint32_t ssrc = base + (uint32_t)((i * 3 + idx) % SSRC_RACE_SPAN);
    channel_t *r = channel_find_by_ssrc(g_ssrc_race_table, ssrc);
    /* ssrc can change between match and this check, expected race.
       only forbidden outcome is a slot outside known set */
    if (r && r != g_ssrc_race_a && r != g_ssrc_race_b)
      atomic_store_explicit(&g_ssrc_race_bad, 1, memory_order_relaxed);
  }
  return NULL;
}

START_TEST(channel_find_by_ssrc_race_no_corruption) {
  channel_table_t *t = channel_table_new(2, 0, 0);
  pthread_t writer, readers[SSRC_RACE_READER_THREADS];
  long i;

  g_ssrc_race_table = t;
  g_ssrc_race_a = lookup_ip(t, "239.9.9.1", 5000);
  g_ssrc_race_b = lookup_ip(t, "239.9.9.2", 5000);
  atomic_store_explicit(&g_ssrc_race_bad, 0, memory_order_relaxed);

  pthread_create(&writer, NULL, ssrc_race_writer, NULL);
  for (i = 0; i < SSRC_RACE_READER_THREADS; i++)
    pthread_create(&readers[i], NULL, ssrc_race_reader, (void *)i);

  pthread_join(writer, NULL);
  for (i = 0; i < SSRC_RACE_READER_THREADS; i++)
    pthread_join(readers[i], NULL);

  ck_assert_int_eq(atomic_load_explicit(&g_ssrc_race_bad, memory_order_relaxed), 0);
  channel_table_free(t);
}
END_TEST

#define LOOKUP_RACE_THREADS 8

static channel_table_t *g_lookup_race_table;
static channel_t *g_lookup_race_results[LOOKUP_RACE_THREADS];

static void *lookup_race_worker(void *arg) {
  long idx = (long)arg;
  g_lookup_race_results[idx] = lookup_ip(g_lookup_race_table, "239.6.6.6", 5000);
  return NULL;
}

START_TEST(channel_lookup_race_single_winner) {
  pthread_t th[LOOKUP_RACE_THREADS];
  long i;

  g_lookup_race_table = channel_table_new(4, 0, 0);
  for (i = 0; i < LOOKUP_RACE_THREADS; i++)
    pthread_create(&th[i], NULL, lookup_race_worker, (void *)i);
  for (i = 0; i < LOOKUP_RACE_THREADS; i++)
    pthread_join(th[i], NULL);

  ck_assert_ptr_nonnull(g_lookup_race_results[0]);
  for (i = 1; i < LOOKUP_RACE_THREADS; i++)
    ck_assert_ptr_eq(g_lookup_race_results[i], g_lookup_race_results[0]);

  channel_table_free(g_lookup_race_table);
}
END_TEST

START_TEST(channel_table_at_reflects_in_use_and_capacity) {
  channel_table_t *t = channel_table_new(3, 0, 0);
  channel_t *a;
  size_t i;

  ck_assert_uint_eq(channel_table_capacity(t), 3u);
  for (i = 0; i < channel_table_capacity(t); i++)
    ck_assert_ptr_null(channel_table_at(t, i)); /* nothing claimed yet */

  a = lookup_ip(t, "239.1.1.1", 5000);
  ck_assert_ptr_nonnull(a);
  {
    int found = 0;
    for (i = 0; i < channel_table_capacity(t); i++) {
      channel_t *c = channel_table_at(t, i);
      if (c) {
        ck_assert_ptr_eq(c, a);
        found++;
      }
    }
    ck_assert_int_eq(found, 1);
  }

  channel_table_reap(t, -1); /* max_age -1: always "older" than now */
  for (i = 0; i < channel_table_capacity(t); i++)
    ck_assert_ptr_null(channel_table_at(t, i)); /* reclaimed, no longer visible */

  channel_table_free(t);
}
END_TEST

static void mk_addr(struct sockaddr_in *a, unsigned port) {
  memset(a, 0, sizeof *a);
  a->sin_family = AF_INET;
  a->sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, "10.0.0.1", &a->sin_addr);
}

START_TEST(channel_hned_seen_no_collision_for_same_address) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  struct sockaddr_in a;
  uint32_t out[CHANNEL_HNED_COLLISION_MAX];

  mk_addr(&a, 6001);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, NULL, 0);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, NULL, 0); /* same ssrc, same address again */

  ck_assert_uint_eq(channel_hned_collisions(c, out, CHANNEL_HNED_COLLISION_MAX, 60), 0u);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_hned_seen_detects_collision_from_different_address) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  struct sockaddr_in a, b;
  uint32_t out[CHANNEL_HNED_COLLISION_MAX];
  size_t n;

  mk_addr(&a, 6001);
  mk_addr(&b, 6002);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, NULL, 0);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&b, sizeof b, NULL, 0); /* same ssrc, different port */

  n = channel_hned_collisions(c, out, CHANNEL_HNED_COLLISION_MAX, 60);
  ck_assert_uint_eq(n, 1u);
  ck_assert_uint_eq(out[0], 0xABCDu);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_hned_seen_cname_takes_precedence_over_address) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  struct sockaddr_in a, b;
  uint32_t out[CHANNEL_HNED_COLLISION_MAX];

  mk_addr(&a, 6001);
  mk_addr(&b, 6002);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, "same@host", 9);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&b, sizeof b, "same@host", 9); /* address changed, cname didn't: same HNED, no collision */

  ck_assert_uint_eq(channel_hned_collisions(c, out, CHANNEL_HNED_COLLISION_MAX, 60), 0u);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_hned_seen_detects_collision_from_different_cname) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  struct sockaddr_in a;
  uint32_t out[CHANNEL_HNED_COLLISION_MAX];
  size_t n;

  mk_addr(&a, 6001);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, "alice@host", 10);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, "bob@host", 8); /* same ssrc, same address, different cname: real collision */

  n = channel_hned_collisions(c, out, CHANNEL_HNED_COLLISION_MAX, 60);
  ck_assert_uint_eq(n, 1u);
  ck_assert_uint_eq(out[0], 0xABCDu);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_hned_seen_falls_back_to_address_without_cname) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  struct sockaddr_in a, b;
  uint32_t out[CHANNEL_HNED_COLLISION_MAX];
  size_t n;

  mk_addr(&a, 6001);
  mk_addr(&b, 6002);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, NULL, 0); /* never sent SDES */
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&b, sizeof b, NULL, 0); /* different address, still no cname: fallback applies */

  n = channel_hned_collisions(c, out, CHANNEL_HNED_COLLISION_MAX, 60);
  ck_assert_uint_eq(n, 1u);
  ck_assert_uint_eq(out[0], 0xABCDu);
  channel_table_free(t);
}
END_TEST

START_TEST(channel_hned_collisions_ages_out_past_max_age) {
  channel_table_t *t = channel_table_new(1, 0, 0);
  channel_t *c = lookup_ip(t, "239.1.1.1", 5000);
  struct sockaddr_in a, b;
  uint32_t out[CHANNEL_HNED_COLLISION_MAX];

  mk_addr(&a, 6001);
  mk_addr(&b, 6002);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&a, sizeof a, NULL, 0);
  channel_hned_seen(c, 0xABCDu, (const struct sockaddr *)&b, sizeof b, NULL, 0);

  ck_assert_uint_eq(channel_hned_collisions(c, out, CHANNEL_HNED_COLLISION_MAX, 60), 1u);
  ck_assert_uint_eq(channel_hned_collisions(c, out, CHANNEL_HNED_COLLISION_MAX, -1), 0u); /* max_age -1: already "too old" */
  channel_table_free(t);
}
END_TEST

START_TEST(channel_hned_collisions_are_per_channel) {
  channel_table_t *t = channel_table_new(2, 0, 0);
  channel_t *c1 = lookup_ip(t, "239.1.1.1", 5000);
  channel_t *c2 = lookup_ip(t, "239.1.1.2", 5000);
  struct sockaddr_in a, b;
  uint32_t out[CHANNEL_HNED_COLLISION_MAX];

  mk_addr(&a, 6001);
  mk_addr(&b, 6002);
  channel_hned_seen(c1, 0xABCDu, (const struct sockaddr *)&a, sizeof a, NULL, 0);
  channel_hned_seen(c1, 0xABCDu, (const struct sockaddr *)&b, sizeof b, NULL, 0); /* collision on c1 only */
  channel_hned_seen(c2, 0xABCDu, (const struct sockaddr *)&a, sizeof a, NULL, 0);

  ck_assert_uint_eq(channel_hned_collisions(c1, out, CHANNEL_HNED_COLLISION_MAX, 60), 1u);
  ck_assert_uint_eq(channel_hned_collisions(c2, out, CHANNEL_HNED_COLLISION_MAX, 60), 0u);
  channel_table_free(t);
}
END_TEST

static Suite *channel_suite(void) {
  Suite *s = suite_create("channel");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, channel_lookup_allocates_finds_and_exhausts);
  tcase_add_test(tc, channel_lookup_populates_addr_and_text_group);
  tcase_add_test(tc, channel_store_and_find_ret_ring_round_trips);
  tcase_add_test(tc, channel_find_returns_zero_when_ring_disabled);
  tcase_add_test(tc, channel_ring_wraps_and_overwrites_oldest);
  tcase_add_test(tc, channel_find_by_ssrc_locates_the_right_channel);
  tcase_add_test(tc, channel_lookup_by_resolve_slot_finds_the_right_channel);
  tcase_add_test(tc, channel_lookup_by_resolve_slot_rejects_out_of_range);
  tcase_add_test(tc, channel_lookup_by_resolve_slot_returns_null_for_unclaimed_slot);
  tcase_add_test(tc, channel_lookup_by_resolve_slot_survives_reap_and_rediscovery);
  tcase_add_test(tc, channel_fcc_cache_tracks_rap_and_entries);
  tcase_add_test(tc, channel_has_rap_stays_zero_when_fcc_disabled);
  tcase_add_test(tc, channel_table_reap_frees_stale_channels);
  tcase_add_test(tc, channel_lookup_reclaim_resets_rtx_seq_mc);
  tcase_add_test(tc, channel_table_reap_step_bounds_work_per_call);
  tcase_add_test(tc, channel_lookup_churn_triggers_rebuild_without_hanging);
  tcase_add_test(tc, channel_table_at_reflects_in_use_and_capacity);
  tcase_add_test(tc, channel_hned_seen_no_collision_for_same_address);
  tcase_add_test(tc, channel_hned_seen_detects_collision_from_different_address);
  tcase_add_test(tc, channel_hned_seen_cname_takes_precedence_over_address);
  tcase_add_test(tc, channel_hned_seen_detects_collision_from_different_cname);
  tcase_add_test(tc, channel_hned_seen_falls_back_to_address_without_cname);
  tcase_add_test(tc, channel_hned_collisions_ages_out_past_max_age);
  tcase_add_test(tc, channel_hned_collisions_are_per_channel);
  tcase_add_test(tc, channel_seqlock_race_no_torn_reads);
  tcase_add_test(tc, channel_fcc_cache_race_no_torn_reads);
  tcase_add_test(tc, channel_find_by_ssrc_race_no_corruption);
  tcase_add_test(tc, channel_lookup_race_single_winner);
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
