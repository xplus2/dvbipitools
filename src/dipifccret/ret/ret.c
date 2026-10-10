/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdlib.h>

#include "lib/demux/rtcp.h"
#include "lib/helper/log.h"
#include "lib/mux/rtcp_build.h"
#include "lib/mux/rtx.h"
#include "lib/sys/ioutil.h"

#include "../version.h"
#include "ratelimit.h"
#include "ret.h"
#include "rtx_session_table.h"

#define RET_LIMIT_SLOTS 4096 /* per-source buckets, direct-mapped */
#define RET_BUCKET_SECONDS 3 /* depth = rate x this */
#define RET_DEDUP_SLOTS 4096 /* power of two */

struct ret_ctx {
  channel_table_t *channels;
  unsigned char rtx_pt;
  ret_send_fn send_mc;
  void *send_mc_user;
  ret_send_unicast_fn send_unicast;
  void *send_unicast_user;
  rtx_session_table_t *rtx_clients; /* unicast RTX seq, one session per client address, F.3.2.1 */
  ratelimit_t *limiter; /* NULL: unlimited */
  unsigned client_rate;
  unsigned mc_dedup_ms; /* 0: off */
  _Atomic uint64_t limited_total;
  _Atomic uint64_t dedup_key[RET_DEDUP_SLOTS]; /* (ssrc,seq), bit 63 = used */
  _Atomic int64_t dedup_ms[RET_DEDUP_SLOTS];
};

ret_ctx_t *ret_ctx_new(channel_table_t *channels, unsigned char rtx_pt, size_t max_ret_clients, ret_send_fn send_mc, ret_send_unicast_fn send_unicast, void *user) {
  ret_ctx_t *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->channels = channels;
  r->rtx_pt = rtx_pt;
  r->send_mc = send_mc;
  r->send_mc_user = user;
  r->send_unicast = send_unicast;
  r->send_unicast_user = user;
  r->rtx_clients = rtx_session_table_new(max_ret_clients);
  if (!r->rtx_clients) {
    free(r);
    return NULL;
  }
  return r;
}

void ret_ctx_free(ret_ctx_t *r) {
  if (!r) return;
  ratelimit_free(r->limiter);
  rtx_session_table_free(r->rtx_clients);
  free(r);
}

int ret_ctx_set_limits(ret_ctx_t *r, unsigned client_rate, unsigned mc_dedup_ms) {
  if (client_rate && !r->limiter) {
    r->limiter = ratelimit_new(RET_LIMIT_SLOTS);
    if (!r->limiter) return -1;
  }
  r->client_rate = client_rate;
  r->mc_dedup_ms = mc_dedup_ms;
  return 0;
}

/* log on powers of two only */
static void note_limited(ret_ctx_t *r) {
  uint64_t n = atomic_fetch_add_explicit(&r->limited_total, 1, memory_order_relaxed) + 1;
  if ((n & (n - 1)) == 0)
    log_line(TOOL_NAME ": NACK repairs rate-limited, %llu so far", (unsigned long long)n);
}

/* 1 if MC repaired within window, else marks now. unlocked: race = one extra / skipped cp */
static int mc_recently_repaired(ret_ctx_t *r, uint32_t ssrc, uint16_t seq, int64_t now) {
  uint64_t key = (1ULL << 63) | ((uint64_t)ssrc << 16) | seq;
  size_t i = (size_t)((key * 0x9E3779B97F4A7C15ULL) >> 40) & (RET_DEDUP_SLOTS - 1);

  if (!r->mc_dedup_ms) return 0;
  if (atomic_load_explicit(&r->dedup_key[i], memory_order_relaxed) == key &&
      now - atomic_load_explicit(&r->dedup_ms[i], memory_order_relaxed) < (int64_t)r->mc_dedup_ms)
    return 1;
  atomic_store_explicit(&r->dedup_ms[i], now, memory_order_relaxed);
  atomic_store_explicit(&r->dedup_key[i], key, memory_order_relaxed);
  return 0;
}

void ret_ctx_reap_step(ret_ctx_t *r, time_t max_age_s, size_t max_scan) {
  rtx_session_table_reap_step(r->rtx_clients, max_age_s, max_scan);
}

size_t ret_ctx_active_clients(ret_ctx_t *r) {
  return rtx_session_table_active_count(r->rtx_clients);
}

static void repair_one(ret_ctx_t *r, channel_t *c, const channel_slot_t *slot) {
  unsigned char out[12 + 2 + CHANNEL_MAX_PAYLOAD];
  size_t n = rtx_build(&c->rtx_seq_mc, c->ssrc, r->rtx_pt, slot->timestamp, slot->seq, slot->payload, slot->payload_len, out, sizeof out);
  if (n > 0)
    r->send_mc(c, out, n, slot->dscp, r->send_mc_user);
}

static void repair_one_unicast(ret_ctx_t *r, const channel_t *c, const channel_slot_t *slot, int fd, const struct sockaddr *from, socklen_t fromlen) {
  unsigned char out[12 + 2 + CHANNEL_MAX_PAYLOAD];
  rtx_session_slot_t *session = rtx_session_table_get(r->rtx_clients, from, fromlen);
  size_t n;
  if (!session) return;
  n = rtx_build(&session->seq, c->ssrc, r->rtx_pt, slot->timestamp, slot->seq, slot->payload, slot->payload_len, out, sizeof out);
  if (n > 0) r->send_unicast(fd, from, fromlen, out, n, slot->dscp, r->send_unicast_user);
}

static int take_budget(ret_ctx_t *r, const struct sockaddr *from, int64_t now) {
  return ratelimit_take(r->limiter, from, r->client_rate, r->client_rate * RET_BUCKET_SECONDS, 1, now) == 1;
}

static int repair_seq(ret_ctx_t *r, channel_t *c, uint16_t seq, int fd, const struct sockaddr *from, socklen_t fromlen, int64_t now) {
  channel_slot_t slot;
  if (!channel_find(c, seq, &slot)) return 1;
  if (!take_budget(r, from, now)) return 0;
  repair_one_unicast(r, c, &slot, fd, from, fromlen);
  if (!mc_recently_repaired(r, c->ssrc, seq, now)) repair_one(r, c, &slot);
  return 1;
}

static void repair_range(ret_ctx_t *r, channel_t *c, uint16_t start, uint16_t end) {
  uint16_t seq = start;
  for (;;) {
    channel_slot_t slot;
    if (channel_find(c, seq, &slot)) repair_one(r, c, &slot);
    if (seq == end) break;
    seq++;
  }
}

void ret_handle_nack(ret_ctx_t *r, const rtcp_nack_t *nack, int fd, const struct sockaddr *from, socklen_t fromlen) {
  channel_t *c = channel_find_by_ssrc(r->channels, nack->media_ssrc);
  unsigned char ff[12 + 4 * RTCP_NACK_MAX_ENTRIES];
  size_t ff_len;
  int64_t now;
  if (!c) return;

  now = now_ms();
  if (!take_budget(r, from, now)) {
    note_limited(r);
    return;
  }

  /* F.5.2 multicast repair/suppression, additional to unicast reply below */
  ff_len = rtcp_build_ff(nack->sender_ssrc, nack->media_ssrc, nack->entry, nack->entry_count, ff, sizeof ff);
  if (ff_len > 0) r->send_mc(c, ff, ff_len, NET_DSCP_SIGNALLING, r->send_mc_user);
  for (size_t i = 0; i < nack->entry_count; i++) {
    uint16_t pid = nack->entry[i].pid;
    uint16_t blp = nack->entry[i].blp;

    /* F.3.1/Figure F.2: always reply directly to requester */
    if (!repair_seq(r, c, pid, fd, from, fromlen, now)) goto limited;
    for (unsigned bit = 0; bit < 16; bit++) {
      if (blp & (1u << bit)) {
        uint16_t seq = (uint16_t)(pid + bit + 1);
        if (!repair_seq(r, c, seq, fd, from, fromlen, now)) goto limited;
      }
    }
  }
  return;

limited:
  note_limited(r);
}

void ret_on_self_detected_gap(ret_ctx_t *r, uint32_t ssrc, uint16_t gap_start, uint16_t gap_end) {
  channel_t *c = channel_find_by_ssrc(r->channels, ssrc);
  rtcp_nack_entry_t entry;
  unsigned char ff[16];
  size_t ff_len;
  if (!c) return;

  entry.pid = gap_start;
  entry.blp = 0; /* signals gap start only; repair below still covers full range */
  ff_len = rtcp_build_ff(0, ssrc, &entry, 1, ff, sizeof ff);
  if (ff_len > 0) r->send_mc(c, ff, ff_len, NET_DSCP_SIGNALLING, r->send_mc_user);
  repair_range(r, c, gap_start, gap_end);
}
