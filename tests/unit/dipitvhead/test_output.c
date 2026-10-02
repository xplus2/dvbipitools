/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "lib/metrics/export.h"
#include "lib/metrics/protocol.h"

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
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.51", 15351, "lo", 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.51", 15351, "lo", 500);
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
  ck_assert_int_eq(o.mc_had_error, 0);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, TS_PER_DGRAM * 188);
  for (i = 0; i < TS_PER_DGRAM; i++)
    ck_assert_uint_eq(rbuf[i * 188 + 2], (unsigned char)i); /* marker byte round-tripped in order */

  bitrate_pacer_free(pacer);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

START_TEST(packet_cb_puts_catch_up_nulls_ahead_of_the_real_packet) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.90", 15490, "lo", 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.90", 15490, "lo", 500);
  bitrate_pacer_t *pacer = bitrate_pacer_new(10000000.0, 1, 0);
  struct timespec ts = {0, 30000000};
  static unsigned char rbuf[262144];
  out_ctx_t o;
  unsigned char pkt[188];
  size_t total = 0;
  ssize_t n;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  nanosleep(&ts, NULL);
  init_out_ctx(&o, send, 0, NULL, pacer);
  make_packet(pkt, 0x55);
  packet_cb(&o, pkt);
  flush_batch(&o);
  ck_assert_uint_gt((unsigned)o.packets, 1u);

  while ((n = mcast_recv(recv, rbuf + total, sizeof rbuf - total, NULL)) > 0) total += (size_t)n;
  ck_assert_uint_eq((unsigned)total, (unsigned)(o.packets * 188));
  ck_assert_uint_eq(rbuf[1], 0x1Fu);
  ck_assert_uint_eq(rbuf[2], 0xFFu);
  ck_assert_uint_eq(rbuf[total - 188 + 2], 0x55u);

  bitrate_pacer_free(pacer);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

static void make_pcr_es_packet(unsigned char pkt[188], unsigned pid, unsigned char cc, uint64_t pcr27) {
  memset(pkt, 0xAB, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x30 | (cc & 0x0F));
  pkt[4] = 7;
  pkt[5] = 0x10;
  pkt[10] = 0x7E;
  ck_assert_int_eq(pcr_packet_write(pkt, pcr27), 0);
}

static void make_es_packet(unsigned char pkt[188], unsigned pid) {
  memset(pkt, 0xCD, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
}

static void init_regenerate(out_ctx_t *o, bitrate_pacer_t *pacer, uint64_t bps) {
  init_out_ctx(o, NULL, 0, NULL, pacer);
  o->pcr_mode = PCR_MODE_REGENERATE;
  pcrclock_init(&o->pcr_clock, bps, 0);
  o->pcr_pkt_ticks = pcrclock_at(&o->pcr_clock, 1) - pcrclock_at(&o->pcr_clock, 0);
  out_pcr_pid_set(o, 0, 0x100);
}

START_TEST(regenerate_follows_output_position_after_latch) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  uint64_t got;

  init_regenerate(&o, pacer, 8000000);
  out_pcr_latch(&o, 9000000);
  make_pcr_es_packet(pkt, 0x100, 1, 9000000);
  packet_cb(&o, pkt);
  ck_assert_int_eq(pcr_packet_read(o.batch + 12, &got), 1);
  ck_assert_uint_eq(got, 9000000u);
  flush_batch(&o);

  make_es_packet(pkt, 0x101);
  for (int i = 0; i < 3; i++) packet_cb(&o, pkt);
  flush_batch(&o);

  make_pcr_es_packet(pkt, 0x100, 2, 123);
  packet_cb(&o, pkt);
  ck_assert_int_eq(pcr_packet_read(o.batch + 12, &got), 1);
  ck_assert_uint_eq(got, 9000000u + 4u * 5076u);
  ck_assert_uint_eq((unsigned)o.pcr_rewritten, 2u);
  ck_assert_uint_eq((unsigned)o.pcr_injected, 0u);
  bitrate_pacer_free(pacer);
}
END_TEST

START_TEST(regenerate_keeps_flags_cc_and_payload_of_pcr_packets) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  unsigned char orig[188];

  init_regenerate(&o, pacer, 8000000);
  out_pcr_latch(&o, 5000);
  make_pcr_es_packet(pkt, 0x100, 9, 5000);
  memcpy(orig, pkt, sizeof pkt);
  packet_cb(&o, pkt);
  ck_assert_mem_eq(o.batch + 12, orig, 6);
  ck_assert_mem_eq(o.batch + 12 + 12, orig + 12, 176);
  bitrate_pacer_free(pacer);
}
END_TEST

START_TEST(preserve_mode_leaves_pcr_untouched_and_injects_nothing) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  uint64_t got;

  init_regenerate(&o, pacer, 8000000);
  o.pcr_mode = PCR_MODE_PRESERVE;
  make_pcr_es_packet(pkt, 0x100, 1, 777);
  packet_cb(&o, pkt);
  ck_assert_int_eq(pcr_packet_read(o.batch + 12, &got), 1);
  ck_assert_uint_eq(got, 777u);
  make_es_packet(pkt, 0x101);
  for (int i = 0; i < 2000; i++) packet_cb(&o, pkt);
  ck_assert_uint_eq((unsigned)o.pcr_injected, 0u);
  ck_assert_uint_eq((unsigned)o.pcr_rewritten, 0u);
  bitrate_pacer_free(pacer);
}
END_TEST

START_TEST(pcr_on_an_untracked_pid_is_left_alone) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  uint64_t got;

  init_regenerate(&o, pacer, 8000000);
  out_pcr_latch(&o, 1000);
  make_pcr_es_packet(pkt, 0x222, 1, 4242);
  packet_cb(&o, pkt);
  ck_assert_int_eq(pcr_packet_read(o.batch + 12, &got), 1);
  ck_assert_uint_eq(got, 4242u);
  ck_assert_uint_eq((unsigned)o.pcr_rewritten, 0u);
  bitrate_pacer_free(pacer);
}
END_TEST

START_TEST(pcr_interval_never_exceeds_forty_ms_when_the_source_sends_none) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  const uint64_t limit = 40ULL * PCR_CLOCK_HZ / 1000ULL;

  init_regenerate(&o, pacer, 8000000);
  out_pcr_latch(&o, 1000000);
  make_pcr_es_packet(pkt, 0x100, 1, 1000000);
  packet_cb(&o, pkt);
  make_es_packet(pkt, 0x101);
  for (int i = 0; i < 5000; i++) {
    uint64_t now;
    packet_cb(&o, pkt);
    now = pcrclock_at(&o.pcr_clock, o.packets - o.pcr_start_index);
    ck_assert_uint_le(pcr_sub(now, o.pcr_pids[0].last_pcr), limit);
  }
  ck_assert_uint_ge((unsigned)o.pcr_injected, 20u);
  ck_assert_uint_le((unsigned)o.pcr_injected, 30u);
  bitrate_pacer_free(pacer);
}
END_TEST

START_TEST(injected_pcr_packets_are_adaptation_only_with_the_pid_cc_and_position_pcr) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.91", 15491, "lo", 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.91", 15491, "lo", 500);
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  static unsigned char rbuf[131072];
  out_ctx_t o;
  unsigned char pkt[188];
  size_t total = 0;
  size_t found = 0;
  ssize_t n;
  pcrclock_t zero;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  init_regenerate(&o, pacer, 8000000);
  out_pcr_latch(&o, 1000000);
  o.mc = send;
  zero = o.pcr_clock;
  make_pcr_es_packet(pkt, 0x100, 5, 1000000);
  packet_cb(&o, pkt);
  make_es_packet(pkt, 0x101);
  for (int i = 0; i < 500; i++) packet_cb(&o, pkt);
  flush_batch(&o);

  while ((n = mcast_recv(recv, rbuf + total, sizeof rbuf - total, NULL)) > 0) total += (size_t)n;
  ck_assert_uint_eq((unsigned)total, (unsigned)(o.packets * 188));
  for (size_t i = 0; i < total / 188; i++) {
    const unsigned char *p = rbuf + i * 188;
    uint64_t got;
    unsigned pid = (unsigned)((p[1] & 0x1F) << 8) | p[2];
    if (pid != 0x100 || i == 0) continue;
    found++;
    ck_assert_uint_eq(p[3], 0x25u);
    ck_assert_uint_eq(p[4], 183u);
    ck_assert_int_eq(pcr_packet_read(p, &got), 1);
    ck_assert_uint_eq(got, pcr_add(1000000, pcrclock_at(&zero, i) - pcrclock_at(&zero, 0)));
  }
  ck_assert_uint_eq((unsigned)found, (unsigned)o.pcr_injected);
  ck_assert_uint_ge((unsigned)found, 2u);
  bitrate_pacer_free(pacer);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

START_TEST(clearing_the_pcr_pid_stops_injection) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];

  init_regenerate(&o, pacer, 8000000);
  out_pcr_latch(&o, 1000000);
  make_pcr_es_packet(pkt, 0x100, 1, 1000000);
  packet_cb(&o, pkt);
  out_pcr_pid_set(&o, 0, 0);
  make_es_packet(pkt, 0x101);
  for (int i = 0; i < 3000; i++) packet_cb(&o, pkt);
  ck_assert_uint_eq((unsigned)o.pcr_injected, 0u);
  bitrate_pacer_free(pacer);
}
END_TEST

START_TEST(pcr_clock_stays_continuous_across_a_registration_gap) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  uint64_t got;

  init_regenerate(&o, pacer, 8000000);
  out_pcr_latch(&o, 1000000);
  make_pcr_es_packet(pkt, 0x100, 1, 1000000);
  packet_cb(&o, pkt);
  flush_batch(&o);
  out_pcr_pid_set(&o, 0, 0);
  make_es_packet(pkt, 0x101);
  for (int i = 0; i < 10; i++) packet_cb(&o, pkt);
  flush_batch(&o);
  out_pcr_pid_set(&o, 0, 0x100);
  make_pcr_es_packet(pkt, 0x100, 2, 55);
  packet_cb(&o, pkt);
  ck_assert_int_eq(pcr_packet_read(o.batch + 12, &got), 1);
  ck_assert_uint_eq(got, 1000000u + 11u * 5076u);
  bitrate_pacer_free(pacer);
}
END_TEST

START_TEST(pcr_before_the_latch_is_left_alone_and_a_second_latch_is_ignored) {
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  unsigned char pkt[188];
  uint64_t got;
  uint64_t clock;

  init_regenerate(&o, pacer, 8000000);
  make_pcr_es_packet(pkt, 0x100, 1, 777);
  packet_cb(&o, pkt);
  ck_assert_int_eq(pcr_packet_read(o.batch + 12, &got), 1);
  ck_assert_uint_eq(got, 777u);
  ck_assert_int_eq(out_pcr_clock(&o, &clock), 0);
  flush_batch(&o);

  out_pcr_latch(&o, 5000000);
  out_pcr_latch(&o, 99);
  ck_assert_int_eq(out_pcr_clock(&o, &clock), 1);
  ck_assert_uint_eq(clock, 5000000u);
  bitrate_pacer_free(pacer);
}
END_TEST

typedef struct {
  int found;
  uint64_t value;
} seen_t;

static void scan_snapshot(const unsigned char *buf, size_t len, seen_t *seen, size_t n_ids, const metrics_id_t *ids) {
  metrics_reader_t rd;
  metrics_hdr_t hdr;
  metrics_id_t id;
  char label[64];
  uint64_t value;
  if (metrics_reader_init(&rd, buf, len, &hdr)) return;
  while (metrics_reader_next(&rd, &id, label, sizeof label, &value) > 0)
    for (size_t i = 0; i < n_ids; i++)
      if (ids[i] == id) {
        seen[i].found = 1;
        seen[i].value = value;
      }
}

static void emit_and_scan(const out_ctx_t *o, const ts_metrics_t *tsm, seen_t *seen, size_t n_ids, const metrics_id_t *ids) {
  char path[] = "/tmp/dipitvhead_metrics_XXXXXX";
  struct sockaddr_un addr;
  metrics_exporter_t mx;
  input_metrics_t in;
  unsigned char buf[65536];
  int fd = mkstemp(path);
  int sock;
  ssize_t n;

  ck_assert_int_ge(fd, 0);
  close(fd);
  unlink(path);
  sock = socket(AF_UNIX, SOCK_DGRAM, 0);
  ck_assert_int_ge(sock, 0);
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, path);
  ck_assert_int_eq(bind(sock, (struct sockaddr *)&addr, sizeof addr), 0);
  memset(&mx, 0, sizeof mx);
  metrics_exporter_init(&mx, METRICS_COMPONENT_TVHEAD, "t1", path, 1.0);
  memset(&in, 0, sizeof in);
  emit_metrics(&mx, 100.0, o, 1, 1, &in, 1, tsm, NULL);
  while ((n = recv(sock, buf, sizeof buf, MSG_DONTWAIT)) > 0) scan_snapshot(buf, (size_t)n, seen, n_ids, ids);
  metrics_exporter_close(&mx);
  close(sock);
  unlink(path);
}

START_TEST(regenerate_metrics_are_exported) {
  static const metrics_id_t ids[] = {METRICS_ID_TV_PCR_REWRITTEN_TOTAL, METRICS_ID_TV_PCR_INJECTED_TOTAL, METRICS_ID_TV_PES_RETIMED_TOTAL,
                                     METRICS_ID_TV_PES_RETIME_SKIPPED_TOTAL, METRICS_ID_TV_RETIME_RELATCHES_TOTAL, METRICS_ID_TV_HOLD_FORCED_RELEASES_TOTAL,
                                     METRICS_ID_TV_SCTE35_ADJUSTED_TOTAL, METRICS_ID_TV_RELEASE_LEAD_MIN_MICROSECONDS, METRICS_ID_TV_RELEASE_LEAD_MAX_MICROSECONDS};
  seen_t seen[9];
  out_ctx_t o;
  ts_metrics_t tsm;

  memset(&o, 0, sizeof o);
  memset(&tsm, 0, sizeof tsm);
  memset(seen, 0, sizeof seen);
  o.pcr_mode = PCR_MODE_REGENERATE;
  o.pcr_rewritten = 5;
  o.pcr_injected = 7;
  tsm.pcr_rewritten_total = 2;
  tsm.pes_retimed_total = 11;
  tsm.pes_retime_skipped_total = 3;
  tsm.retime_relatches_total = 4;
  tsm.hold_forced_total = 6;
  tsm.scte35_adjusted_total = 8;
  tsm.release_lead_seen = 1;
  tsm.release_lead_min_us = -250;
  tsm.release_lead_max_us = 700000;
  emit_and_scan(&o, &tsm, seen, 9, ids);
  for (int i = 0; i < 9; i++) ck_assert_msg(seen[i].found, "id index %d missing", i);
  ck_assert_uint_eq(seen[0].value, 7u);
  ck_assert_uint_eq(seen[1].value, 7u);
  ck_assert_uint_eq(seen[2].value, 11u);
  ck_assert_uint_eq(seen[3].value, 3u);
  ck_assert_uint_eq(seen[4].value, 4u);
  ck_assert_uint_eq(seen[5].value, 6u);
  ck_assert_uint_eq(seen[6].value, 8u);
  ck_assert_int_eq(metrics_unzigzag(seen[7].value), -250);
  ck_assert_int_eq(metrics_unzigzag(seen[8].value), 700000);
}
END_TEST

START_TEST(rebase_metrics_omit_the_regenerate_only_series) {
  static const metrics_id_t ids[] = {METRICS_ID_TV_PCR_REWRITTEN_TOTAL, METRICS_ID_TV_PES_RETIMED_TOTAL, METRICS_ID_TV_PCR_INJECTED_TOTAL,
                                     METRICS_ID_TV_HOLD_FORCED_RELEASES_TOTAL, METRICS_ID_TV_RELEASE_LEAD_MIN_MICROSECONDS};
  seen_t seen[5];
  out_ctx_t o;
  ts_metrics_t tsm;

  memset(&o, 0, sizeof o);
  memset(&tsm, 0, sizeof tsm);
  memset(seen, 0, sizeof seen);
  o.pcr_mode = PCR_MODE_REBASE;
  tsm.pcr_rewritten_total = 9;
  tsm.pes_retimed_total = 4;
  emit_and_scan(&o, &tsm, seen, 5, ids);
  ck_assert_int_eq(seen[0].found, 1);
  ck_assert_uint_eq(seen[0].value, 9u);
  ck_assert_int_eq(seen[1].found, 1);
  ck_assert_int_eq(seen[2].found, 0);
  ck_assert_int_eq(seen[3].found, 0);
  ck_assert_int_eq(seen[4].found, 0);
}
END_TEST

START_TEST(preserve_metrics_omit_every_retime_series) {
  static const metrics_id_t ids[] = {METRICS_ID_TV_PCR_REWRITTEN_TOTAL, METRICS_ID_TV_PES_RETIMED_TOTAL, METRICS_ID_TV_PCR_INJECTED_TOTAL};
  seen_t seen[3];
  out_ctx_t o;
  ts_metrics_t tsm;

  memset(&o, 0, sizeof o);
  memset(&tsm, 0, sizeof tsm);
  memset(seen, 0, sizeof seen);
  emit_and_scan(&o, &tsm, seen, 3, ids);
  for (int i = 0; i < 3; i++) ck_assert_int_eq(seen[i].found, 0);
}
END_TEST

START_TEST(flush_batch_is_a_no_op_when_empty) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.52", 15352, "lo", 1);
  bitrate_pacer_t *pacer = bitrate_pacer_new(0, 0, 0);
  out_ctx_t o;
  ck_assert_ptr_nonnull(send);
  init_out_ctx(&o, send, 0, NULL, pacer);

  flush_batch(&o); /* batch_count == 0: must not touch mc or pacer */
  ck_assert_int_eq(o.batch_count, 0);
  ck_assert_uint_eq(o.packets, 0u);
  ck_assert_int_eq(o.mc_had_error, 0);

  bitrate_pacer_free(pacer);
  mcast_close(send);
}
END_TEST

START_TEST(flush_batch_prefixes_rtp_header_when_rtp_enabled) {
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.53", 15353, "lo", 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.53", 15353, "lo", 500);
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
  mcast_t *send = mcast_open_send(AF_INET, "239.7.9.54", 15354, "lo", 1);
  mcast_t *recv = mcast_open(AF_INET, "239.7.9.54", 15354, "lo", 500);
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

static void init_mcast_cfg(config_t *cfg, const char *group, unsigned port, int rtp) {
  memset(cfg, 0, sizeof *cfg);
  cfg->family = AF_INET;
  strcpy(cfg->mcast_group, group);
  cfg->mcast_port = port;
  cfg->ttl = 1;
  cfg->rtp = rtp;
}

START_TEST(output_open_fails_cleanly_for_an_unusable_multicast_destination) {
  config_t cfg;
  out_ctx_t o;

  init_mcast_cfg(&cfg, "not-an-address", 15461, 1);
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(tvhead_output_open(&cfg, &o), -1);
  ck_assert_ptr_null(o.mc);
  ck_assert_ptr_null(o.rtph);
  tvhead_output_close(&o);
}
END_TEST

START_TEST(output_close_on_a_zeroed_context_is_a_noop) {
  out_ctx_t o;

  memset(&o, 0, sizeof o);
  tvhead_output_close(&o);
  ck_assert_ptr_null(o.insp);
}
END_TEST

typedef struct {
  const char *name;
  int rtp;
  unsigned al_fec_l;
  int expect_rtph;
  int expect_fec;
} sink_case_t;

static const sink_case_t sink_cases[] = {
    {"plain udp", 0, 0, 0, 0},
    {"rtp without fec", 1, 0, 1, 0},
    {"rtp with fec", 1, 5, 1, 1},
    {"fec is ignored for plain udp", 0, 5, 0, 0},
};

START_TEST(output_open_builds_the_configured_sinks) {
  const sink_case_t *c = &sink_cases[_i];
  config_t cfg;
  out_ctx_t o;

  init_mcast_cfg(&cfg, "239.7.9.91", 15491, c->rtp);
  cfg.al_fec_l = c->al_fec_l;
  cfg.al_fec_d = c->al_fec_l;
  cfg.al_fec_port = 15462;
  memset(&o, 0, sizeof o);
  ck_assert_msg(tvhead_output_open(&cfg, &o) == 0, "%s: open failed", c->name);
  ck_assert_msg(o.mc != NULL, "%s: no multicast sender", c->name);
  ck_assert_msg(o.rtp == c->rtp, "%s: rtp flag %d", c->name, o.rtp);
  ck_assert_msg((o.rtph != NULL) == c->expect_rtph, "%s: rtp header state", c->name);
  ck_assert_msg((o.fec_enc != NULL) == c->expect_fec, "%s: fec encoder state", c->name);
  ck_assert_msg((o.fec_mc != NULL) == c->expect_fec, "%s: fec sender state", c->name);
  ck_assert_msg(o.rist == NULL && o.srt == NULL, "%s: unexpected relay sinks", c->name);
  tvhead_output_close(&o);
}
END_TEST

START_TEST(output_open_with_no_destination_and_inspection_succeeds) {
  config_t cfg;
  out_ctx_t o;

  memset(&cfg, 0, sizeof cfg);
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(tvhead_output_open(&cfg, &o), 0);
  ck_assert_ptr_null(o.mc);
  tvhead_output_close(&o);

  cfg.metrics_inspect_ts = METRICS_INSPECT_TS_BASIC;
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(tvhead_output_open(&cfg, &o), 0);
  ck_assert_ptr_nonnull(o.insp);
  tvhead_output_close(&o);
  ck_assert_ptr_null(o.insp);
}
END_TEST

START_TEST(relay_sinks_fail_without_their_backends_and_leave_no_leaks) {
  config_t cfg;
  out_ctx_t o;

  init_mcast_cfg(&cfg, "239.7.9.91", 15491, 0);
  strcpy(cfg.rist_uri[0], "rist://127.0.0.1:19100");
  cfg.n_rist = 1;
  ck_assert_ptr_null(tvhead_rist_open(&cfg));
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(tvhead_output_open(&cfg, &o), -1);
  ck_assert_ptr_nonnull(o.mc);
  ck_assert_ptr_null(o.rist);
  tvhead_output_close(&o);

  cfg.n_rist = 0;
  strcpy(cfg.srt_host[0], "127.0.0.1");
  cfg.srt_port[0] = 19101;
  cfg.n_srt = 1;
  ck_assert_ptr_null(tvhead_srt_open(&cfg));
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(tvhead_output_open(&cfg, &o), -1);
  ck_assert_ptr_null(o.srt);
  tvhead_output_close(&o);
}
END_TEST

static Suite *output_suite(void) {
  Suite *s = suite_create("dipitvhead_output");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, packet_cb_batches_until_ts_per_dgram_then_flushes);
  tcase_add_test(tc, packet_cb_puts_catch_up_nulls_ahead_of_the_real_packet);
  tcase_add_test(tc, regenerate_follows_output_position_after_latch);
  tcase_add_test(tc, regenerate_keeps_flags_cc_and_payload_of_pcr_packets);
  tcase_add_test(tc, preserve_mode_leaves_pcr_untouched_and_injects_nothing);
  tcase_add_test(tc, pcr_on_an_untracked_pid_is_left_alone);
  tcase_add_test(tc, pcr_interval_never_exceeds_forty_ms_when_the_source_sends_none);
  tcase_add_test(tc, injected_pcr_packets_are_adaptation_only_with_the_pid_cc_and_position_pcr);
  tcase_add_test(tc, clearing_the_pcr_pid_stops_injection);
  tcase_add_test(tc, pcr_clock_stays_continuous_across_a_registration_gap);
  tcase_add_test(tc, pcr_before_the_latch_is_left_alone_and_a_second_latch_is_ignored);
  tcase_add_test(tc, regenerate_metrics_are_exported);
  tcase_add_test(tc, rebase_metrics_omit_the_regenerate_only_series);
  tcase_add_test(tc, preserve_metrics_omit_every_retime_series);
  tcase_add_test(tc, flush_batch_is_a_no_op_when_empty);
  tcase_add_test(tc, flush_batch_prefixes_rtp_header_when_rtp_enabled);
  tcase_add_test(tc, send_null_packet_emits_a_valid_null_pid_packet);
  tcase_add_test(tc, output_open_fails_cleanly_for_an_unusable_multicast_destination);
  tcase_add_test(tc, output_close_on_a_zeroed_context_is_a_noop);
  tcase_add_loop_test(tc, output_open_builds_the_configured_sinks, 0, (int)(sizeof sink_cases / sizeof sink_cases[0]));
  tcase_add_test(tc, output_open_with_no_destination_and_inspection_succeeds);
  tcase_add_test(tc, relay_sinks_fail_without_their_backends_and_leave_no_leaks);
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
